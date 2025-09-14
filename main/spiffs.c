#include "spiffs.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "fs_utils";

// ====== Config: SDSPI pins for ESP32-WROOM DevKit (adjust as needed) ======
// VSPI defaults: SCLK=18, MISO=19, MOSI=23, CS=5
#ifndef SDCARD_SPI_HOST
#define SDCARD_SPI_HOST     SPI3_HOST     // VSPI
#endif
#ifndef SDCARD_PIN_MOSI
#define SDCARD_PIN_MOSI     23
#endif
#ifndef SDCARD_PIN_MISO
#define SDCARD_PIN_MISO     19
#endif
#ifndef SDCARD_PIN_SCLK
#define SDCARD_PIN_SCLK     18
#endif
#ifndef SDCARD_PIN_CS
#define SDCARD_PIN_CS       5
#endif

// ====== SPIFFS ======
esp_err_t spiffs_init(const char *base_path, size_t max_files, bool format_if_mount_failed)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL, // use default "storage" partition of type "data,spiffs"
        .max_files = (int)max_files,
        .format_if_mount_failed = format_if_mount_failed
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "SPIFFS partition not found. Check partitions.csv (type=data, subtype=spiffs).");
        return ret;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted at %s: total=%u, used=%u bytes", base_path, (unsigned)total, (unsigned)used);
    } else {
        ESP_LOGW(TAG, "esp_spiffs_info failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

void spiffs_deinit(const char *base_path)
{
    (void)base_path; // base_path not required to unregister
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

static esp_err_t list_dir_generic(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "opendir(%s) failed: errno=%d", dir_path, errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Listing: %s", dir_path);
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
       char fullpath[512];  // was 256
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                ESP_LOGI(TAG, "  <DIR> %s", ent->d_name);
            } else {
                ESP_LOGI(TAG, "  %8ld  %s", (long)st.st_size, ent->d_name);
            }
        } else {
            ESP_LOGI(TAG, "  ?        %s", ent->d_name);
        }
    }
    closedir(dir);
    return ESP_OK;
}

esp_err_t spiffs_list_dir(const char *dir_path)
{
    return list_dir_generic(dir_path);
}

static esp_err_t read_file_generic(const char *path, char **out_buf, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed: errno=%d", path, errno);
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return ESP_FAIL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) {
        free(buf);
        ESP_LOGE(TAG, "fread short (%zu/%ld)", rd, len);
        return ESP_FAIL;
    }
    buf[len] = '\0';
    if (out_buf) *out_buf = buf; else free(buf);
    if (out_len) *out_len = (size_t)len;
    return ESP_OK;
}

esp_err_t spiffs_read_file(const char *path, char **out_buf, size_t *out_len)
{
    return read_file_generic(path, out_buf, out_len);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

esp_err_t spiffs_write_file(const char *path, const void *data, size_t len, bool overwrite)
{
    if (!overwrite && file_exists(path)) {
        ESP_LOGE(TAG, "File exists and overwrite=false: %s", path);
        return ESP_ERR_INVALID_STATE;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) for write failed: errno=%d", path, errno);
        return ESP_FAIL;
    }
    size_t wr = fwrite(data, 1, len, f);
    fclose(f);
    if (wr != len) {
        ESP_LOGE(TAG, "fwrite short (%zu/%zu)", wr, len);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wrote %zu bytes to %s", len, path);
    return ESP_OK;
}

// ====== SD (SDSPI) ======
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_init(const char *base_path)
{
    if (s_card) {
        ESP_LOGW(TAG, "SD already mounted");
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SDCARD_SPI_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDCARD_PIN_MOSI,
        .miso_io_num = SDCARD_PIN_MISO,
        .sclk_io_num = SDCARD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_MASTER
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDCARD_PIN_CS;
    slot_config.host_id = host.slot;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card FATFS at %s: %s", base_path, esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD mounted at %s", base_path);
    return ESP_OK;
}

void sdcard_deinit(const char *base_path)
{
    (void)base_path;
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(NULL, s_card);
        s_card = NULL;
        // Free the SPI bus used
        spi_bus_free(SDCARD_SPI_HOST);
        ESP_LOGI(TAG, "SD unmounted");
    }
}

esp_err_t sdcard_list_dir(const char *dir_path)
{
    return list_dir_generic(dir_path);
}

esp_err_t sdcard_read_file(const char *path, char **out_buf, size_t *out_len)
{
    return read_file_generic(path, out_buf, out_len);
}

// ====== Cross: SD -> SPIFFS ======
esp_err_t spiffs_copy_from_sd(const char *sd_path, const char *spiffs_path, bool overwrite)
{
    char *buf = NULL;
    size_t len = 0;
    esp_err_t ret = sdcard_read_file(sd_path, &buf, &len);
    if (ret != ESP_OK) return ret;

    ret = spiffs_write_file(spiffs_path, buf, len, overwrite);
    free(buf);
    return ret;
}

esp_err_t sd_to_spiffs_move(const char *sd_base,
                            const char *sd_in_path,
                            const char *spiffs_base,
                            const char *spiffs_out_path,
                            bool overwrite,
                            bool move)
{
    (void)sd_base;
    (void)spiffs_base;

    // 0) quick ignore of macOS resource-fork files
    const char *base = strrchr(sd_in_path, '/');
    base = base ? base + 1 : sd_in_path;
    if (strncmp(base, "._", 2) == 0) {
        ESP_LOGW(TAG, "Skipping resource-fork file: %s", sd_in_path);
        return ESP_ERR_INVALID_ARG;
    }

    // 1) Ensure SD is mounted (no-op if already)
    esp_err_t ret = sdcard_init("/sd");
    if (ret != ESP_OK) return ret;

    // 2) Stat source file, get size
    struct stat src_st = {0};
    if (stat(sd_in_path, &src_st) != 0 || !S_ISREG(src_st.st_mode)) {
        ESP_LOGE(TAG, "Failed to stat SD file: %s (errno=%d)", sd_in_path, errno);
        return ESP_FAIL;
    }

    // 3) Mount SPIFFS if needed
    // Try to open dest for append to probe VFS; if fails, attempt mount.
    FILE *probe = fopen(spiffs_out_path, "ab");
    if (!probe) {
        ret = spiffs_init("/spiffs", 8, false);
        if (ret != ESP_OK) return ret;
    } else {
        fclose(probe);
    }

    // 4) Check SPIFFS free space
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        size_t free_bytes = (used <= total) ? (total - used) : 0;
        if (src_st.st_size > (ssize_t)free_bytes) {
            ESP_LOGE(TAG, "Not enough SPIFFS space: need %ld, have %u bytes",
                     (long)src_st.st_size, (unsigned)free_bytes);
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_LOGW(TAG, "esp_spiffs_info failed (%s); skipping capacity pre-check",
                 esp_err_to_name(ret));
    }

    // 5) If overwrite=false and dest exists, bail
    if (!overwrite) {
        struct stat dst_st;
        if (stat(spiffs_out_path, &dst_st) == 0 && S_ISREG(dst_st.st_mode)) {
            ESP_LOGE(TAG, "Dest exists and overwrite=false: %s", spiffs_out_path);
            return ESP_ERR_INVALID_STATE;
        }
    }

    // 6) Stream copy in chunks (no big malloc)
    FILE *fin = fopen(sd_in_path, "rb");
    if (!fin) {
        ESP_LOGE(TAG, "fopen(%s) failed: errno=%d", sd_in_path, errno);
        return ESP_FAIL;
    }
    FILE *fout = fopen(spiffs_out_path, "wb");
    if (!fout) {
        ESP_LOGE(TAG, "fopen(%s) for write failed: errno=%d", spiffs_out_path, errno);
        fclose(fin);
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc(4096);
    if (!buf) {
        ESP_LOGE(TAG, "malloc buffer failed");
        fclose(fin);
        fclose(fout);
        return ESP_ERR_NO_MEM;
    }

    size_t total_written = 0;
    for (;;) {
        size_t rd = fread(buf, 1, 4096, fin);
        if (rd == 0) break;
        size_t wr = fwrite(buf, 1, rd, fout);
        if (wr != rd) {
            ESP_LOGE(TAG, "Short write to %s (wrote %zu of %zu)", spiffs_out_path, wr, rd);
            free(buf); fclose(fin); fclose(fout);
            return ESP_FAIL;
        }
        total_written += wr;
    }

    free(buf);
    fclose(fin);
    fclose(fout);
    ESP_LOGI(TAG, "Stream-copied %u bytes: %s -> %s",
             (unsigned)total_written, sd_in_path, spiffs_out_path);

    // 7) Move semantics (optional delete from SD)
    if (move) {
        if (remove(sd_in_path) != 0) {
            ESP_LOGW(TAG, "remove(%s) failed: errno=%d (copied but not removed)",
                     sd_in_path, errno);
        } else {
            ESP_LOGI(TAG, "Moved: %s -> %s", sd_in_path, spiffs_out_path);
        }
    }

    return ESP_OK;
}

#include "spiffs.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "SPIFFS_MOD";

static SemaphoreHandle_t s_lock = NULL;
static const char *s_label = NULL;          // partition label used at mount
static char s_base_path[32] = SPIFFS_MOUNT_PATH;
static bool s_mounted = false;

#define LOCK()   do { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); } while (0)
#define UNLOCK() do { if (s_lock) xSemaphoreGive(s_lock); } while (0)

void spiffs_lock(void)   { LOCK(); }
void spiffs_unlock(void) { UNLOCK(); }

esp_err_t spiffs_mount(const char *partition_label,
                       const char *base_path,
                       int max_files,
                       bool format_if_mount_failed)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "Failed to create FS mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_mounted) {
        ESP_LOGW(TAG, "SPIFFS already mounted at %s", s_base_path);
        return ESP_OK;
    }

    if (base_path && base_path[0]) {
        size_t n = strlcpy(s_base_path, base_path, sizeof(s_base_path));
        if (n >= sizeof(s_base_path)) {
            ESP_LOGE(TAG, "base_path too long");
            return ESP_ERR_INVALID_ARG;
        }
    }

    s_label = partition_label;  // may be NULL for default "spiffs"

    esp_vfs_spiffs_conf_t conf = {
        .base_path = s_base_path,
        .partition_label = s_label,
        .max_files = max_files,
        .format_if_mount_failed = format_if_mount_failed
    };

    LOCK();
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    UNLOCK();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_spiffs_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total=0, used=0;
    ret = esp_spiffs_info(s_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Mounted SPIFFS at %s (total=%u, used=%u, free=%u)",
                 s_base_path, (unsigned)total, (unsigned)used, (unsigned)(total-used));
    } else {
        ESP_LOGW(TAG, "esp_spiffs_info failed: %s", esp_err_to_name(ret));
    }

    s_mounted = true;
    return ESP_OK;
}

void spiffs_unmount(void)
{
    if (!s_mounted) return;

    LOCK();
    esp_vfs_spiffs_unregister(s_label);
    UNLOCK();

    s_mounted = false;

    if (s_lock) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

esp_err_t spiffs_create_empty_file(const char *path, bool truncate_if_exists)
{
    if (!path || !path[0]) return ESP_ERR_INVALID_ARG;

    // First: check if it exists
    LOCK();
    FILE *f = fopen(path, truncate_if_exists ? "w" : "wx"); // "wx" fails if exists
    if (!f) {
        UNLOCK();
        if (!truncate_if_exists) {
            ESP_LOGE(TAG, "File exists or cannot create: %s", path);
            return ESP_ERR_INVALID_STATE;
        } else {
            ESP_LOGE(TAG, "Failed to create/truncate file: %s", path);
            return ESP_FAIL;
        }
    }
    int rc = fclose(f);
    UNLOCK();

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to close new file: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created empty file: %s", path);
    return ESP_OK;
}

esp_err_t spiffs_get_info(size_t *out_total, size_t *out_used)
{
    size_t total=0, used=0;
    LOCK();
    esp_err_t ret = esp_spiffs_info(s_label, &total, &used);
    UNLOCK();
    if (ret != ESP_OK) return ret;
    if (out_total) *out_total = total;
    if (out_used)  *out_used  = used;
    return ESP_OK;
}

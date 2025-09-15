#include "spiffs.h"
#include "heartbeat.h"
#include "compression.h"
#include "global.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

/* Global Lock for Synchronizing access to SPI Flash.*/
SemaphoreHandle_t spi_flash_lock = NULL;

/* File path for Config.txt on SD Card. */
#define SD_CONFIG_FILE "/sd/config.txt"

/* File path for output file in SPIFFS*/
#define SPIFFS_OUTPUT_FILE  "/spiffs/sensor_data.csv"

/* File path for compressed file in SPIFFS*/
#define SPIFFS_COMPRESSED_FILE  "/spiffs/compressed_output.csv"

/* Testing: Name of file to move from SD to SPI Flash emulating background work. */
#define SD_INPUT_FILE  "/sd/Lucas_Sample_Data.csv"


static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

/* Default Settings. */
static int  g_comp_interval_ms = 30000;
static char g_comp_algo[16]    = "rle";

static void parse_config_commands(const char *path,
                                  const char *spiffs_data_file,
                                  const char *spiffs_compressed_file,
                                  gpio_num_t heartbeat_pin)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW("CONFIG", "No config file at %s (skipping)", path);
        return;
    }
    ESP_LOGI("CONFIG", "Reading %s", path);

    char line[192];
    while (fgets(line, sizeof(line), f)) {
        
        /* Cut down newline*/
        size_t n = strlen(s);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')){ 
            line[--n] = '\0'; 
        }

        if (line[0] == '\0' || line[0] == '#'){
            continue;
        }

        /* Developer Command: sdcloud.set_expected_write_frequency(1000) */
        if (strncmp(line, "sdcloud.set_expected_write_frequency", 36) == 0) {
            int ms = 0;
            if (sscanf(line, "sdcloud.set_expected_write_frequency(%d)", &ms) == 1 && ms > 0) {
                ESP_LOGI("CONFIG", "heartbeat period -> %d ms", ms);
                heartbeat_set_period_ms(ms);
            }
            continue;
        }

        /* Developer Command:  sdcloud.run_heartbeat */
        if (strcmp(line, "sdcloud.run_heartbeat") == 0) {
            ESP_LOGI("CONFIG", "starting heartbeat");
            (void)heartbeat_start(spiffs_data_file, heartbeat_pin, 1000);
            continue;
        }

        /* Developer Command: sdcloud.stop_heartbeat */
        if (strcmp(line, "sdcloud.stop_heartbeat") == 0) {
            ESP_LOGI("CONFIG", "stopping heartbeat");
            (void)heartbeat_stop();
            continue;
        }

        /* Developer Command: sdcloud.set_compression_algorithm(rle OR delta) */
        if (strncmp(line, "sdcloud.set_compression_algorithm", 33) == 0) {
            char algo[16] = {0};
            if (sscanf(line, "sdcloud.set_compression_algorithm(%15[^)])", algo) == 1) {
                strncpy(g_comp_algo, algo, sizeof(g_comp_algo)-1);
                g_comp_algo[sizeof(g_comp_algo) - 1] = '\0';
                ESP_LOGI("CONFIG", "compression algo -> %s", g_comp_algo);
                compression_set_algorithm(g_comp_algo);
            }
            continue;
        }

        /* Developer Command: sdcloud.set_compression_frequency(30000)*/
        if (strncmp(line, "sdcloud.set_compression_frequency(", 32) == 0) {
            int ms = 0;
            if (sscanf(line, "sdcloud.set_compression_frequency((%d)", &ms) == 1 && ms > 0) {
                g_comp_interval_ms = ms;
                ESP_LOGI("CONFIG", "compression frequency( -> %d ms", g_comp_interval_ms);
                compression_set_frequency_ms(g_comp_interval_ms);
            }   
            continue;
        }

        /* Developer Command: sdcloud.run_compression */
        if (strcmp(line, "sdcloud.run_compression") == 0) {
            ESP_LOGI("CONFIG", "starting compression (%s, %d ms)", g_comp_algo, g_comp_interval_ms);
            (void)compression_start(spiffs_data_file, spiffs_compressed_file, g_comp_interval_ms, g_comp_algo);
            continue;
        }

        /* Developer Command: sdcloud.stop_compression */
        if (strcmp(line, "sdcloud.stop_compression") == 0) {
            ESP_LOGI("CONFIG", "stopping compression");
            (void)compression_stop();
            continue;
        }

        ESP_LOGW("CONFIG", "incorrect command: %s", line);
    }
    fclose(f);
}

void app_main(void) {
    /* Mount SPIFFS */
    ESP_ERROR_CHECK(spiffs_init("/spiffs", 8, true));
    spiffs_list_file_sys("/spiffs");

    /* Create global lock for SPI Flash Synchronization. */
    spi_flash_lock = xSemaphoreCreateMutex();
    if (spi_flash_lock == NULL) {
        ESP_LOGE("APP", "Failed to create SPI flash lock");
        return;
    }

    /* Only do if the output file doesn't exist on SPI Flash -> Testing Shortcut. */
    if (!file_exists(SPIFFS_OUTPUT_FILE)) {
        esp_err_t r = sdcard_init("/sd");
        if (r == ESP_OK) {
            r = sd_to_spiffs_move("/sd", SD_INPUT_FILE, "/spiffs", SPIFFS_OUTPUT_FILE, true, false);
            if (r != ESP_OK) {
                ESP_LOGW("APP", "Seed failed: %s", esp_err_to_name(r));
            } else {
                /* Create an empty file if it doesn't exist and couldn't transfer from SD Card. */
                const char *hdr = ""; // or put a header line if you like
        (       void)spiffs_write_file(SPIFFS_OUTPUT_FILE, hdr, strlen(hdr), true);
            }
        }
    }

    sdcard_list_file_sys("/sd");
    sdcard_breakdown("/sd");

    /* Parse Config File & Extract Developer Commands. */
    parse_config_commands(SD_CONFIG_FILE, SPIFFS_OUTPUT_FILE, SPIFFS_COMPRESSED_FILE, GPIO_NUM_2);

    /* Testing: Control Test Writer outside of Config File. */
    ESP_ERROR_CHECK(test_writer_start(SPIFFS_OUTPUT_FILE, 5000, "Test entry."));

    /* Sanity Check.*/
    spiffs_list_file_sys("/spiffs");
}
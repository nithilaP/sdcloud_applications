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

// Name of the file on the SD card you want to move
#define SD_INPUT_FILE  "/sd/Lucas_Sample_Data.csv"

// Config File on SD
#define SD_CONFIG_FILE "/sd/config.txt"

// Name to use on SPIFFS after moving
#define SPIFFS_OUTPUT_FILE  "/spiffs/sensor_data.csv"
#define SPIFFS_COMPRESSED_FILE  "/spiffs/compressed_output.csv"

static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

// parsing the config file 
// --- simple config parser (drop-in) ---

// simple local state for compression settings; tweak as you like
static int  g_comp_interval_ms = 30000;
static char g_comp_algo[16]    = "rle";

// helper: trim newline
static void trim_nl(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) { s[--n] = '\0'; }
}

// call this after SD is mounted
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
        trim_nl(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        // sdcloud.set_expected_write_frequency(1234)
        if (strncmp(line, "sdcloud.set_expected_write_frequency", 36) == 0) {
            int ms = 0;
            if (sscanf(line, "sdcloud.set_expected_write_frequency(%d)", &ms) == 1 && ms > 0) {
                ESP_LOGI("CONFIG", "heartbeat period -> %d ms", ms);
                heartbeat_set_period_ms(ms);
            }
            continue;
        }

        // sdcloud.run_heartbeat
        if (strcmp(line, "sdcloud.run_heartbeat") == 0) {
            ESP_LOGI("CONFIG", "starting heartbeat");
            (void)heartbeat_start(spiffs_data_file, heartbeat_pin, 1000); // current period used; can be changed by set_expected_write_frequency()
            continue;
        }

        // sdcloud.stop_heartbeat
        if (strcmp(line, "sdcloud.stop_heartbeat") == 0) {
            ESP_LOGI("CONFIG", "stopping heartbeat");
            #ifdef HEARTBEAT_HAS_STOP
            (void)heartbeat_stop();
            #else
            ESP_LOGW("CONFIG", "heartbeat_stop() not available; ignoring");
            #endif
            continue;
        }

        // sdcloud.set_compression_algorithm(rle|delta)
        if (strncmp(line, "sdcloud.set_compression_algorithm", 33) == 0) {
            char algo[16] = {0};
            if (sscanf(line, "sdcloud.set_compression_algorithm(%15[^)])", algo) == 1) {
                strncpy(g_comp_algo, algo, sizeof(g_comp_algo)-1);
                g_comp_algo[sizeof(g_comp_algo)-1] = '\0';
                ESP_LOGI("CONFIG", "compression algo -> %s", g_comp_algo);
                #ifdef COMPRESSION_HAS_SET_ALGO
                compression_set_algorithm(g_comp_algo);
                #endif
            }
            continue;
        }

        // (optional) sdcloud.set_compression_interval(30000)
        if (strncmp(line, "sdcloud.set_compression_interval", 32) == 0) {
            int ms = 0;
            if (sscanf(line, "sdcloud.set_compression_interval(%d)", &ms) == 1 && ms > 0) {
                g_comp_interval_ms = ms;
                ESP_LOGI("CONFIG", "compression interval -> %d ms", g_comp_interval_ms);
                #ifdef COMPRESSION_HAS_SET_INTERVAL
                compression_set_interval_ms(g_comp_interval_ms);
                #endif
            }
            continue;
        }

        // sdcloud.run_compression
        if (strcmp(line, "sdcloud.run_compression") == 0) {
            ESP_LOGI("CONFIG", "starting compression (%s, %d ms)", g_comp_algo, g_comp_interval_ms);
            (void)compression_start(spiffs_data_file, spiffs_compressed_file,
                                    g_comp_interval_ms, g_comp_algo);
            continue;
        }

        // sdcloud.stop_compression
        if (strcmp(line, "sdcloud.stop_compression") == 0) {
            ESP_LOGI("CONFIG", "stopping compression");
            #ifdef COMPRESSION_HAS_STOP
            (void)compression_stop();
            #else
            ESP_LOGW("CONFIG", "compression_stop() not available; ignoring");
            #endif
            continue;
        }

        ESP_LOGW("CONFIG", "unknown command: %s", line);
    }
    fclose(f);
}

void app_main(void) {
    // Mount SPIFFS
    ESP_ERROR_CHECK(spiffs_init("/spiffs", 8, true));
    spiffs_list_file_sys("/spiffs");

    // Create global lock
    spi_flash_lock = xSemaphoreCreateMutex();
    if (spi_flash_lock == NULL) {
        ESP_LOGE("APP", "Failed to create SPI flash lock");
        return;
    }

    // OPTIONAL: seed CSV once from SD if missing (you already had this)
    // if (!file_exists(SPIFFS_OUTPUT_FILE)) {
        esp_err_t r = sdcard_init("/sd");
        if (r == ESP_OK) {
            r = sd_to_spiffs_move("/sd", SD_INPUT_FILE,
                                  "/spiffs", SPIFFS_OUTPUT_FILE,
                                  true, false);
            // sdcard_breakdown("/sd");
            if (r != ESP_OK) ESP_LOGW("APP", "Seed failed: %s", esp_err_to_name(r));
        }
    // }

    sdcard_list_file_sys("/sd");

    // Ensure the CSV exists (create empty if still missing)
    if (!file_exists(SPIFFS_OUTPUT_FILE)) {
        const char *hdr = ""; // or put a header line if you like
        (void)spiffs_write_file(SPIFFS_OUTPUT_FILE, hdr, strlen(hdr), true);
    }

    // Set up
    parse_config_commands(SD_CONFIG_FILE, SPIFFS_OUTPUT_FILE, SPIFFS_COMPRESSED_FILE, GPIO_NUM_2);

    // (optional) keep the test writer independent of config
    ESP_ERROR_CHECK(test_writer_start(SPIFFS_OUTPUT_FILE, 5000, "Test entry."));

    // // Start HEARTBEAT: read SPIFFS file every 1000 ms, pulse GPIO2 on growth
    // ESP_ERROR_CHECK(heartbeat_start(SPIFFS_OUTPUT_FILE, GPIO_NUM_2, 1000));

    // // Start TEST WRITER: append a line every 5s so you can see the heartbeat pulse
    // ESP_ERROR_CHECK(test_writer_start(SPIFFS_OUTPUT_FILE, 5000, "Test entry."));

    // // You can adjust the heartbeat period later if needed:
    // // heartbeat_set_period_ms(1500);

    // // start compression every 30s with "rle" (or "delta")
    // ESP_ERROR_CHECK(compression_start(SPIFFS_OUTPUT_FILE,
    //                                   SPIFFS_COMPRESSED_FILE,
    //                                   30000,
    //                                   "rle"));

    spiffs_list_file_sys("/spiffs");
}
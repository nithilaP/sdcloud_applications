#include <stdio.h>
#include "spiffs.h"
#include "heartbeat.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>
#include "driver/gpio.h"
#include "global.h"

SemaphoreHandle_t spi_flash_lock = NULL;

// Name of the file on the SD card you want to move
#define SD_INPUT_FILE  "/sd/Lucas_Sample_Data.csv"

// Name to use on SPIFFS after moving
#define SPIFFS_OUTPUT_FILE  "/spiffs/sensor_data.csv"

static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

void app_main(void) {
    // Mount SPIFFS
    ESP_ERROR_CHECK(spiffs_init("/spiffs", 8, true));
    spiffs_list_dir("/spiffs");

    // Create global lock
    spi_flash_lock = xSemaphoreCreateMutex();
    if (spi_flash_lock == NULL) {
        ESP_LOGE("APP", "Failed to create SPI flash lock");
        return;
    }

    // OPTIONAL: seed CSV once from SD if missing (you already had this)
    if (!file_exists(SPIFFS_OUTPUT_FILE)) {
        esp_err_t r = sdcard_init("/sd");
        if (r == ESP_OK) {
            r = sd_to_spiffs_move("/sd", SD_INPUT_FILE,
                                  "/spiffs", SPIFFS_OUTPUT_FILE,
                                  true, false);
            sdcard_deinit("/sd");
            if (r != ESP_OK) ESP_LOGW("APP", "Seed failed: %s", esp_err_to_name(r));
        }
    }

    // Ensure the CSV exists (create empty if still missing)
    if (!file_exists(SPIFFS_OUTPUT_FILE)) {
        const char *hdr = ""; // or put a header line if you like
        (void)spiffs_write_file(SPIFFS_OUTPUT_FILE, hdr, strlen(hdr), true);
    }

    // Start HEARTBEAT: read SPIFFS file every 1000 ms, pulse GPIO2 on growth
    ESP_ERROR_CHECK(heartbeat_start(SPIFFS_OUTPUT_FILE, GPIO_NUM_2, 1000));

    // Start TEST WRITER: append a line every 5s so you can see the heartbeat pulse
    ESP_ERROR_CHECK(test_writer_start(SPIFFS_OUTPUT_FILE, 5000, "Test entry."));

    // You can adjust the heartbeat period later if needed:
    // heartbeat_set_period_ms(1500);

    spiffs_list_dir("/spiffs");
}
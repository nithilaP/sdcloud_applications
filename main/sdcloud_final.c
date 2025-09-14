#include <stdio.h>
#include "spiffs.h"
#include "esp_log.h"
#include <string.h>

// Name of the file on the SD card you want to move
#define SD_INPUT_FILE  "/sd/Lucas_Sample_Data.csv"

// Name to use on SPIFFS after moving
#define SPIFFS_OUTPUT_FILE  "/spiffs/sensor_data.csv"


void app_main(void) {
    // Mount SPIFFS at /spiffs
    ESP_ERROR_CHECK(spiffs_init("/spiffs", 8, true));
    spiffs_list_dir("/spiffs");

    // Write and read a quick test file
    const char *hello = "hello from spiffs\n";
    ESP_ERROR_CHECK(spiffs_write_file("/spiffs/test.txt", hello, strlen(hello), true));

    char *buf = NULL; size_t len = 0;
    ESP_ERROR_CHECK(spiffs_read_file("/spiffs/test.txt", &buf, &len));
    ESP_LOGI("APP", "SPIFFS read (%u bytes): %s", (unsigned)len, buf);
    free(buf);

    // SD test: mount /sd, list, copy /sd/input.bin to /spiffs/input.bin and delete original
    esp_err_t r = sd_to_spiffs_move("/sd", SD_INPUT_FILE,
                                "/spiffs", SPIFFS_OUTPUT_FILE,
                                true, true);

    spiffs_list_dir("/spiffs");

    if (r != ESP_OK) {
        ESP_LOGE("APP", "SD->SPIFFS copy failed: %s", esp_err_to_name(r));
        // you can choose to return, continue, or handle differently
    }


    // Done
    // sdcard_deinit("/sd");       // if you want to unmount SD
    // spiffs_deinit("/spiffs");   // if you want to unmount SPIFFS

}


/* Verify partitions after mounting. */
/* 
size_t total = 0, used = 0;       
esp_err_t ret = esp_spiffs_info(NULL, &total, &used); // NULL = default "spiffs" label
if (ret == ESP_OK) {
    ESP_LOGI("SPIFFS", "total=%u used=%u free=%u", (unsigned)total, (unsigned)used, (unsigned)(total - used));
} else {
    ESP_LOGE("SPIFFS", "esp_spiffs_info failed: %s", esp_err_to_name(ret));
}
*/
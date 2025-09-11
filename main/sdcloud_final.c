#include <stdio.h>
#include "spiffs.h"
#include "nvs_flash.h"

void app_main(void) {
    // init NVS (always first)
    ESP_ERROR_CHECK(nvs_flash_init());

    // mount SPIFFS
    ESP_ERROR_CHECK(spiffs_mount(NULL, NULL, 5, true));

    // Verify mounting

    size_t total=0, used=0;
ESP_ERROR_CHECK(spiffs_get_info(&total, &used));
printf("SPIFFS total=%u used=%u free=%u\n", (unsigned)total, (unsigned)used, (unsigned)(total-used));


    // create empty test file
    ESP_ERROR_CHECK(spiffs_create_empty_file("/spiffs/test.txt", true));
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
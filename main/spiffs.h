#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default mount path if you don't pass one to spiffs_mount()
#ifndef SPIFFS_MOUNT_PATH
#define SPIFFS_MOUNT_PATH "/spiffs"
#endif

// Mount (register) SPIFFS at base_path (NULL -> SPIFFS_MOUNT_PATH).
// partition_label: NULL for default "spiffs" partition.
// format_if_mount_failed: true is handy during bring-up.
// max_files: how many files can be open at once.
esp_err_t spiffs_mount(const char *partition_label,
                       const char *base_path,
                       int max_files,
                       bool format_if_mount_failed);

// Unmount (unregister VFS) and destroy the internal mutex.
void spiffs_unmount(void);

// Create an empty file (0 bytes). If it exists:
//  - if truncate_if_exists = true  -> truncate to 0
//  - else                          -> return ESP_ERR_INVALID_STATE
esp_err_t spiffs_create_empty_file(const char *path, bool truncate_if_exists);

// Get total/used bytes for the mounted SPIFFS.
esp_err_t spiffs_get_info(size_t *out_total, size_t *out_used);

// Optional: public lock/unlock if you need to group multiple FS ops atomically.
void spiffs_lock(void);
void spiffs_unlock(void);

#ifdef __cplusplus
}
#endif

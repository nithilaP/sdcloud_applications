#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* SPIFFS Operations */

/*
 * Mount SPIFFS at base path.
 */
esp_err_t spiffs_init(const char *base_path, size_t max_files, bool format_if_mount_failed);

/*
 * Unmount SPIFFS.
 */
void spiffs_breakdown();

/*
 * List files in SPIFFS.
 */
esp_err_t spiffs_list_file_sys(const char *dir_path);

/*
 * Read file into buffer (malloced).
 * Used to read file data from SPIFFS for app layer processing.
 */
esp_err_t spiffs_read_file(const char *path, char **out_buf, size_t *out_len);

/*
 * Write buffer into file.
 * Used for uploading app layer processed data back to SPIFFS.
 */
esp_err_t spiffs_write_file(const char *path, const void *data, size_t len, bool overwrite);

/* SD Card Operations.*/

/*
 * Mount SD Card at base path.
 */
esp_err_t sdcard_init(const char *base_path);

/*
 * Unmount SD Card.
 */
void sdcard_breakdown(const char *base_path);

/*
 * List files under SD Card.
 */
esp_err_t sdcard_list_file_sys(const char *dir_path);

/*
 * Used for backend testing. 
 * Can transfer the data file from SDCard to SPIFFS to set up environment 
 * mimicking real world data flow (sensing data would be transferred to SPIFFS outside of app layer.)
 */
esp_err_t sd_to_spiffs_move(const char *sd_base, const char *sd_in_path,const char *spiffs_base, const char *spiffs_out_path, bool overwrite, bool move);

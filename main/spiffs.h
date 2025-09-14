#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==== SPIFFS ====
/**
 * Mount SPIFFS at base_path (e.g. "/spiffs").
 * max_files: number of simultaneously open files allowed.
 * If format_if_mount_failed=true, will format partition on mount error.
 */
esp_err_t spiffs_init(const char *base_path, size_t max_files, bool format_if_mount_failed);

/** Unmount SPIFFS previously mounted at base_path. */
void spiffs_deinit(const char *base_path);

/** List files under a directory (e.g. "/spiffs"). */
esp_err_t spiffs_list_dir(const char *dir_path);

/**
 * Read entire file into a malloc'd buffer (NUL-terminated).
 * Caller must free(*out_buf). out_len excludes the final NUL.
 */
esp_err_t spiffs_read_file(const char *path, char **out_buf, size_t *out_len);

/**
 * Write a buffer to a file. If overwrite=false and file exists, returns ESP_ERR_INVALID_STATE.
 * Creates parent dirs not handled—use flat paths on SPIFFS.
 */
esp_err_t spiffs_write_file(const char *path, const void *data, size_t len, bool overwrite);

/** Copy a file from SD (sd_path) to SPIFFS (spiffs_path). */
esp_err_t spiffs_copy_from_sd(const char *sd_path, const char *spiffs_path, bool overwrite);

// ==== SD card (for testing) ====
// Uses SDSPI by default. Adjust the pin macros in spiffs.c if needed.

/**
 * Mount SD card at base_path (e.g. "/sd").
 * Uses SDSPI by default with typical DevKitC pins; see pin macros in .c.
 */
esp_err_t sdcard_init(const char *base_path);

/** Unmount SD card. */
void sdcard_deinit(const char *base_path);

/** List files under a directory on SD (e.g. "/sd"). */
esp_err_t sdcard_list_dir(const char *dir_path);

/** Read entire file on SD into malloc'd buffer; caller frees like spiffs_read_file. */
esp_err_t sdcard_read_file(const char *path, char **out_buf, size_t *out_len);

// ==== Convenience test flow ====
// 1) init+mount SD, list root
// 2) read input file (sd_in_path)
// 3) ensure SPIFFS mounted at spiffs_base (init if needed)
// 4) write to SPIFFS at spiffs_out_path
// 5) (optional) delete source from SD if move=true
esp_err_t sd_to_spiffs_move(const char *sd_base,
                            const char *sd_in_path,
                            const char *spiffs_base,
                            const char *spiffs_out_path,
                            bool overwrite,
                            bool move);

#ifdef __cplusplus
}
#endif

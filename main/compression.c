#include "compression.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "global.h"   // extern SemaphoreHandle_t spi_flash_lock

static const char *TAG = "compress";

static TaskHandle_t s_task = NULL;
static char s_algo[16] = "rle";
static int  s_interval_ms = 30000;
static char s_in[128];
static char s_out[128];

// ---- RLE compression (takes/gives spi_flash_lock directly) ----
static void run_rle_compression(const char *input_file, const char *output_file) {
    if (!spi_flash_lock ||
        xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "RLE: lock timeout");
        return;
    }

    FILE *in = fopen(input_file, "r");
    FILE *out = in ? fopen(output_file, "w") : NULL;

    if (!in || !out) {
        ESP_LOGE(TAG, "RLE: fopen failed (in=%p, out=%p)", (void*)in, (void*)out);
        if (in) fclose(in);
        if (out) fclose(out);
        xSemaphoreGive(spi_flash_lock);
        return;
    }

    char prev_line[256] = "";
    char curr_line[256];
    int count = 0;

    while (fgets(curr_line, sizeof(curr_line), in)) {
        if (strcmp(curr_line, prev_line) == 0) {
            count++;
        } else {
            if (count > 0) {
                fprintf(out, "%s,%d\n", prev_line, count);
            }
            strncpy(prev_line, curr_line, sizeof(prev_line)-1);
            prev_line[sizeof(prev_line)-1] = '\0';
            count = 1;
        }
    }
    if (count > 0) {
        fprintf(out, "%s,%d\n", prev_line, count);
    }

    fclose(in);
    fclose(out);
    xSemaphoreGive(spi_flash_lock);

    ESP_LOGI(TAG, "RLE done: %s -> %s", input_file, output_file);
}

// ---- Delta compression (takes/gives spi_flash_lock directly) ----
static void run_delta_encoding_compression(const char *input_file, const char *output_file) {
    if (!spi_flash_lock ||
        xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Delta: lock timeout");
        return;
    }

    FILE *in = fopen(input_file, "r");
    FILE *out = in ? fopen(output_file, "w") : NULL;

    if (!in || !out) {
        ESP_LOGE(TAG, "Delta: fopen failed (in=%p, out=%p)", (void*)in, (void*)out);
        if (in) fclose(in);
        if (out) fclose(out);
        xSemaphoreGive(spi_flash_lock);
        return;
    }

    char line[256];
    float prev_values[32] = {0};
    int is_first_line = 1;

    while (fgets(line, sizeof(line), in)) {
        float values[32];
        int count = 0;
        char *saveptr = NULL;
        char *token = strtok_r(line, ",", &saveptr);
        while (token != NULL && count < 32) {
            values[count++] = strtof(token, NULL);
            token = strtok_r(NULL, ",", &saveptr);
        }

        if (is_first_line) {
            for (int i = 0; i < count; i++) {
                fprintf(out, "%.6f%s", values[i], (i < count - 1) ? "," : "");
                prev_values[i] = values[i];
            }
            fprintf(out, "\n");
            is_first_line = 0;
        } else {
            for (int i = 0; i < count; i++) {
                float delta = values[i] - prev_values[i];
                fprintf(out, "%.6f%s", delta, (i < count - 1) ? "," : "");
                prev_values[i] = values[i];
            }
            fprintf(out, "\n");
        }
    }

    fclose(in);
    fclose(out);
    xSemaphoreGive(spi_flash_lock);

    ESP_LOGI(TAG, "Delta done: %s -> %s", input_file, output_file);
}

// ---- periodic task ----
static void compression_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));

        const char *algo = s_algo;
        ESP_LOGI(TAG, "Compressing (algo=%s): %s -> %s", algo, s_in, s_out);
        if (strcmp(algo, "delta") == 0) {
            run_delta_encoding_compression(s_in, s_out);
        } else {
            run_rle_compression(s_in, s_out);
        }

        // placeholder for upload
        ESP_LOGI(TAG, "(placeholder) would upload: %s", s_out);
    }
}

// ---- API ----
esp_err_t compression_start(const char *input_csv_path,
                            const char *output_csv_path,
                            int interval_ms,
                            const char *algo)
{
    if (!input_csv_path || !output_csv_path || interval_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task) return ESP_OK;

    strncpy(s_in, input_csv_path, sizeof(s_in)-1);
    strncpy(s_out, output_csv_path, sizeof(s_out)-1);
    s_interval_ms = interval_ms;

    if (algo) {
        char lower[16] = {0};
        strncpy(lower, algo, sizeof(lower)-1);
        for (char *p = lower; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 32;
        if (strcmp(lower, "delta") == 0) strncpy(s_algo, "delta", sizeof(s_algo)-1);
        else strncpy(s_algo, "rle", sizeof(s_algo)-1);
    } else {
        strncpy(s_algo, "rle", sizeof(s_algo)-1);
    }

    BaseType_t ok = xTaskCreate(compression_task, "compression_task", 4096, NULL, 4, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

void compression_set_algorithm(const char *algo) {
    if (!algo) return;
    char lower[16] = {0};
    strncpy(lower, algo, sizeof(lower)-1);
    for (char *p = lower; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 32;
    if (strcmp(lower, "delta") == 0) strncpy(s_algo, "delta", sizeof(s_algo)-1);
    else strncpy(s_algo, "rle", sizeof(s_algo)-1);
}

void compression_set_interval(int interval_ms) {
    if (interval_ms > 0) s_interval_ms = interval_ms;
}

void compression_stop(void) {
    if (!s_task) return;
    vTaskDelete(s_task);
    s_task = NULL;
}


// // compression algorithm. 
// #define COMPRESS_INTERVAL_MS 30000
// static char compression_algorithm[16];

// #define COMPRESSED_FILE_PATH SPIFFS_MOUNT "/compressed_output.csv"

// // implement run length encoding.
// void run_rle_compression(const char *input_file, const char *output_file) {
//     FILE *in = fopen(input_file, "r");
//     FILE *out = fopen(output_file, "w");
//     if (!in || !out) {
//         ESP_LOGE(TAG, "Failed to open input or output file for compression.");
//         if (in){
//             fclose(in);   
//         } 
//         if (out){
//             fclose(out);
//         }
//         return;
//     }
//     char prev_line[256] = "";
//     char curr_line[256];
//     int count = 0;
//     while (fgets(curr_line, sizeof(curr_line), in)) {
//         if (strcmp(curr_line, prev_line) == 0) {
//             count++;
//         } else {
//             if (count > 0) {
//                 fprintf(out, "%s,%d\n", prev_line, count);
//             }
//             strcpy(prev_line, curr_line);
//             count = 1;
//         }
//     }
//     if (count > 0) {
//         fprintf(out, "%s,%d\n", prev_line, count);
//     }
//     fclose(in);
//     fclose(out);
//     ESP_LOGI(TAG, "RLE compression complete. Output: %s", output_file);
// }

// // implement delta encoding
// void run_delta_encoding_compression(const char *input_file, const char *output_file) {
//     FILE *in = fopen(input_file, "r");
//     FILE *out = fopen(output_file, "w");
//     if (!in || !out) {
//         ESP_LOGE(TAG, "Failed to open input or output file for delta encoding.");
//         if (in) fclose(in);
//         if (out) fclose(out);
//         return;
//     }

//     char line[256];
//     float prev_values[32]; // Adjust based on max expected columns
//     int is_first_line = 1;

//     while (fgets(line, sizeof(line), in)) {
//         float values[32];
//         int count = 0;
//         char *token = strtok(line, ",");
//         while (token != NULL && count < 32) {
//             values[count++] = strtof(token, NULL);
//             token = strtok(NULL, ",");
//         }

//         if (is_first_line) {
//             for (int i = 0; i < count; i++) {
//                 fprintf(out, "%.6f", values[i]);
//                 if (i < count - 1) fprintf(out, ",");
//                 prev_values[i] = values[i];
//             }
//             fprintf(out, "\n");
//             is_first_line = 0;
//         } else {
//             for (int i = 0; i < count; i++) {
//                 float delta = values[i] - prev_values[i];
//                 fprintf(out, "%.6f", delta);
//                 if (i < count - 1) fprintf(out, ",");
//                 prev_values[i] = values[i];
//             }
//             fprintf(out, "\n");
//         }
//     }

//     fclose(in);
//     fclose(out);
//     ESP_LOGI(TAG, "Delta encoding compression complete. Output: %s", output_file);
// }

// // run compression
// void compress_and_upload_task(void *arg) {
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(COMPRESS_INTERVAL_MS));
//         ESP_LOGI(TAG, "Compressing file with algorithm: %s", compression_algorithm);
//         if (strcmp(compression_algorithm, "rle") == 0) {
//             run_rle_compression(FILE_PATH, COMPRESSED_FILE_PATH);
//         } else if (strcmp(compression_algorithm, "delta") == 0) {
//             run_delta_encoding_compression(FILE_PATH, COMPRESSED_FILE_PATH);
//         } 
//         ESP_LOGI(TAG, "(Placeholder) Uploading %s to cloud...", COMPRESSED_FILE_PATH);
//     }
// }

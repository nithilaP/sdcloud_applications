#include "compression.h"
#include "global.h" 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "compress";

static TaskHandle_t c_task = NULL;
static char compression_algorithm[16] = "rle"; // Default: Run Length Encoding
static char s_in[128];
static char s_out[128];
static int  compression_freq = 30000;

/* RLE Compression Function. */
static void run_rle_compression(const char *input_file, const char *output_file) {
    if (!spi_flash_lock || xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "RLE Compression: Timeout");
        return;
    }

    FILE *in = fopen(input_file, "r");
    FILE *out = NULL;
    if (in){
       out = fopen(output_file, "w");
    }

    if (!in || !out) {
        ESP_LOGE(TAG, "RLE Compression: fopen failed (in=%p, out=%p)", (void*)in, (void*)out);
        if (in){
            fclose(in);
        }
        if (out){
            fclose(out);
        }
        
        xSemaphoreGive(spi_flash_lock);
        return;
    }

    char prev_line[256] = "";
    char curr_line[256];
    int count = 0;

    while (fgets(curr_line, sizeof(curr_line), in)) {
        if (strcmp(curr_line, prev_line) ==0) {
            count++;
        } else {
            if (count > 0) {
                fprintf(out, "%s,%d\n", prev_line, count);
            }
            strncpy(prev_line, curr_line, sizeof(prev_line) - 1);
            prev_line[sizeof(prev_line) - 1] = '\0';
            count =1;
        }
    }
    if (count >0) {
        fprintf(out, "%s,%d\n", prev_line, count);
    }

    fclose(in);
    fclose(out);
    xSemaphoreGive(spi_flash_lock);

    ESP_LOGI(TAG, "RLE Compression done: %s -> %s", input_file, output_file);
}

/* Delta Encoding Compression. */
static void run_delta_encoding_compression(const char *input_file, const char *output_file) {
    if (!spi_flash_lock || xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Delta: lock timeout");
        return;
    }

    FILE *in = fopen(input_file, "r");
    FILE *out = NULL;
    if (in){
       out = fopen(output_file, "w");
    }

    if (!in || !out) {
        ESP_LOGE(TAG, "Delta: fopen failed (in=%p, out=%p)", (void*)in, (void*)out);
        if (in){
            fclose(in);
        } 
        if (out){
            fclose(out);
        }
        xSemaphoreGive(spi_flash_lock);
        return;
    }

    char line[256];
    float prev_values[32] = {0};
    bool first_line = true;

    while (fgets(line, sizeof(line), in)) {
        float values[32];
        int count = 0;
        char *saveptr = NULL;
        char *tkn = strtok_r(line, ",", &saveptr);
        while (tkn != NULL && count < 32) {
            values[count++] = strtof(tkn, NULL);
            tkn = strtok_r(NULL, ",", &saveptr);
        }

        if (first_line) {
            for (int i = 0; i < count; i++) {
                // fprintf(out, "%.6f%s", values[i], (i < count - 1) ? "," : "");
                prev_values[i] = values[i];
            }
            fprintf(out, "\n");
            first_line = false;
        } else {
            for (int i = 0; i < count; i++) {
                float delta = values[i] - prev_values[i];
                // fprintf(out, "%.6f%s", delta, (i < count - 1) ? "," : "");
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

/* Compression Task Func.*/
static void compression_task(void *arg) {
    (void) arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(compression_freq));

        const char *algo = compression_algorithm;
        ESP_LOGI(TAG, "Compressing (algo=%s): %s -> %s", algo, s_in, s_out);
        if (strcmp(algo, "delta") == 0) {
            run_delta_encoding_compression(s_in, s_out);
        } else {
            run_rle_compression(s_in, s_out);
        }
    }
}

/* Developer Functions.*/
esp_err_t compression_start(const char *input_csv_path, const char *output_csv_path, int interval_ms, const char *algo)
{
    if (!input_csv_path || !output_csv_path || interval_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c_task){
        return ESP_OK;
    }

    strncpy(s_in, input_csv_path, sizeof(s_in)-1);
    strncpy(s_out, output_csv_path, sizeof(s_out)-1);
    compression_freq = interval_ms;

    if (algo) {
        char lower[16] = {0};
        strncpy(lower, algo, sizeof(lower)-1);
        for (char *p = lower; *p; ++p){
            if (*p >= 'A' && *p <= 'Z') {
                *p += 32;
            }
        }
        if (strcmp(lower, "delta") == 0) {
            strncpy(compression_algorithm, "delta", sizeof(compression_algorithm) - 1);
        } else {
            strncpy(compression_algorithm, "rle", sizeof(compression_algorithm) - 1);
        }
    } else {
        strncpy(compression_algorithm, "rle", sizeof(compression_algorithm) - 1);
    }

    BaseType_t ok = xTaskCreate(compression_task, "compression_task", 4096, NULL, 4, &c_task);
    if (ok != pdPASS){
        return ESP_FAIL;
    }
    return ESP_OK;
}

void compression_set_algorithm(const char *algo) {
    if (!algo){
        return;
    }
    char lower[16] = {0};
    strncpy(lower, algo, sizeof(lower) - 1);
    for (char *p = lower; *p; ++p){
        if (*p >= 'A' && *p <= 'Z'){
            *p += 32;
        }
    }
    if (strcmp(lower, "delta") == 0){
        strncpy(compression_algorithm, "delta", sizeof(compression_algorithm) - 1);
    } else {
        strncpy(compression_algorithm, "rle", sizeof(compression_algorithm) - 1);
    }
}

void compression_set_interval(int interval_ms) {
    if (interval_ms > 0){
        compression_freq = interval_ms;
    }
}

void compression_stop(void) {
    if (!c_task){
        return;
    }
    vTaskDelete(c_task);
    c_task = NULL;
}
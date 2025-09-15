#include "heartbeat.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "heartbeat";

static TaskHandle_t s_hb_task     = NULL;
static TaskHandle_t s_writer_task = NULL;
static const char  *s_csv_path    = NULL;
static gpio_num_t   s_pin         = GPIO_NUM_NC;
static int          s_period_ms   = 1000;     // default heartbeat period

// ---- helpers ----
static int line_count(const char *file_path) {
    FILE *f = fopen(file_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "open failed: %s", file_path);
        return -1;
    }
    int n = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) n++;
    fclose(f);
    return n;
}

static void ensure_gpio_output(gpio_num_t pin) {
    // Make sure pin is configured as output once; harmless if repeated.
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

// ---- heartbeat task ----
static void heartbeat_task(void *arg) {
    (void)arg;
    int last = line_count(s_csv_path);
    if (last < 0) ESP_LOGW(TAG, "initial read failed (%s)", s_csv_path);

    ensure_gpio_output(s_pin);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(s_period_ms));
        int cur = line_count(s_csv_path);
        if (cur > last) {
            ESP_LOGI(TAG, "data grew: %d -> %d", last, cur);
            gpio_set_level(s_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(s_pin, 0);
            last = cur;
        } else {
            ESP_LOGD(TAG, "no change (%d)", cur);
        }
    }
}

// ---- test writer task ----
typedef struct {
    char path[128];
    int  interval_ms;
    char line[64];
} writer_args_t;

static void append_line(const char *path, const char *text) {
    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "append open failed: %s", path);
        return;
    }
    unsigned long ts = (unsigned long)esp_log_timestamp();
    fprintf(f, "%lu, %s\n", ts, text ? text : "Test entry.");
    fflush(f);
    fclose(f);
}

static void writer_task(void *arg) {
    writer_args_t a = *(writer_args_t *)arg;
    free(arg);  // we copied the contents; free heap for args

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(a.interval_ms));
        append_line(a.path, a.line[0] ? a.line : "Test entry.");
        ESP_LOGI(TAG, "writer: appended to %s", a.path);
    }
}

// ---- API ----
esp_err_t heartbeat_start(const char *csv_path, gpio_num_t pin, int period_ms) {
    if (!csv_path || period_ms <= 0) return ESP_ERR_INVALID_ARG;
    if (s_hb_task) return ESP_OK; // already running

    s_csv_path  = csv_path;
    s_pin       = pin;
    s_period_ms = period_ms;

    BaseType_t ok = xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, &s_hb_task);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

void heartbeat_set_period_ms(int period_ms) {
    if (period_ms > 0) s_period_ms = period_ms;
}

void heartbeat_stop(void) {
    if (!s_hb_task) return;
    vTaskDelete(s_hb_task);
    s_hb_task = NULL;
}

esp_err_t test_writer_start(const char *csv_path, int interval_ms, const char *line_text) {
    if (!csv_path || interval_ms <= 0) return ESP_ERR_INVALID_ARG;
    if (s_writer_task) return ESP_OK; // already running

    writer_args_t *args = (writer_args_t *)calloc(1, sizeof(writer_args_t));
    if (!args) return ESP_ERR_NO_MEM;
    strncpy(args->path, csv_path, sizeof(args->path)-1);
    args->interval_ms = interval_ms;
    if (line_text) strncpy(args->line, line_text, sizeof(args->line)-1);

    BaseType_t ok = xTaskCreate(writer_task, "test_writer", 4096, args, 5, &s_writer_task);
    if (ok != pdPASS) { free(args); return ESP_FAIL; }
    return ESP_OK;
}

void test_writer_stop(void) {
    if (!s_writer_task) return;
    vTaskDelete(s_writer_task);
    s_writer_task = NULL;
}

// // inputs & ouputs 
// #define HEARTBEAT_FREQ 1000 // Defined in ms
// #define OUTPUT_PIN GPIO_NUM_2 // Output pin

// // heartbeat inputs. 
// static int heartbeat_freq_ms = 1000;
// static TaskHandle_t heartbeat_handle = NULL;

// // count lines (helper to detect data).
// int line_count(const char *file_path){
//     FILE *file = fopen(file_path, "r");
//     if (!file){
//         ESP_LOGE(TAG, "Failed to open file: %s", file_path);
//         return -1;
//     }

//     int count = 0; 
//     char buffer[256];

//     while (fgets(buffer, sizeof(buffer), file)) {
//         count++;
//     }

//     fclose(file);
//     return count;
// }

// // output heartbeat at 1 sec intervals. 
// void heartbeat(void *arg){
//     int last_count = line_count(FILE_PATH);
//     if (last_count < 0){
//         ESP_LOGE(TAG, "Initial CSV read failed");
//     }

//     while(1){ // infinite heartbeat
//         vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_FREQ));
//         int cur_count =  line_count(FILE_PATH);

//         if (cur_count > last_count){
//             ESP_LOGI(TAG, "New data detected: %d -> %d entries", last_count, cur_count);

//             // send heartbeat. 
//             gpio_set_level(OUTPUT_PIN, 1);
//             vTaskDelay(pdMS_TO_TICKS(100)); 
//             gpio_set_level(OUTPUT_PIN, 0);   
            
//             last_count = cur_count;
//         } else {
//             ESP_LOGI(TAG, "No new data detected.");
//         }
//     }
// }

// // testing purposes: adds lines to csv
// static const char *TAG_WRITE = "FILE_WRITER"; // log tag to filter for later.
// void test_write(const char *file_path, const char *text) {
//     FILE *file = fopen(file_path, "a");  // append mode to add to end of file.
//     if (!file) {
//         ESP_LOGE(TAG_WRITE, "Failed to open file for writing: %s", file_path);
//         return;
//     }

//     // Get timestamp.
//     unsigned long timestamp = (unsigned long) esp_log_timestamp();

//     // data + timestamp written.
//     if (fprintf(file, "%lu, %s\n", timestamp, text) < 0) {
//         ESP_LOGE(TAG_WRITE, "Failed to write to file.");
//     } else {
//         ESP_LOGI(TAG_WRITE, "Successfully wrote to file: %s", file_path);
//     }

//     fflush(file); // commit immediately.
//     fclose(file);
// }

// // Periodic file writing.
// void test_write_implement(void *arg) {
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(WRITE_INTERVAL_MS)); 
//         ESP_LOGI(TAG_WRITE, "Writing to file...");
//         test_write(FILE_PATH, TEST_STRING);
//     }
// }

#include "heartbeat.h"
#include "global.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "heartbeat";

static TaskHandle_t heartbeat_task = NULL;
static TaskHandle_t writer_task = NULL;
static const char *sensing_data_csv = NULL;
static int heartbeat_freq = 1000;
static gpio_num_t s_gpio_pin = GPIO_NUM_NC;

/* Count Number of lines to see if new data has been added to sensing data file. */
static int line_count(const char *file_path) {
    if (xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW("HEARTBEAT", "Could not take lock to read %s", file_path);
        return -1;
    }
    ESP_LOGW(TAG, "Acquired lock. -> Line Count");
    FILE *f = fopen(file_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "open failed: %s", file_path);
        xSemaphoreGive(spi_flash_lock);
        return -1;
    }
    int n = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) n++;
    fclose(f);

    xSemaphoreGive(spi_flash_lock);
    return n;
}

/* Heartbeat Task. */
static void heartbeat_task_func(void *arg) {
    (void)arg;
    int last = line_count(sensing_data_csv);
    if (last < 0) ESP_LOGW(TAG, "initial read failed (%s)", sensing_data_csv);

    gpio_set_direction(s_gpio_pin, GPIO_MODE_OUTPUT);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(heartbeat_freq));
        int cur = line_count(sensing_data_csv);
        if (cur > last) {
            ESP_LOGI(TAG, "data grew: %d -> %d", last, cur);
            gpio_set_level(s_gpio_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(s_gpio_pin, 0);
            last = cur;
        } else {
            ESP_LOGD(TAG, "no change (%d)", cur);
        }
    }
}

/* Testing Purposes: Writer task that adds lines to sensing data file to mimic real world data collection. */
typedef struct {
    char path[128];
    int  interval_ms;
    char line[64];
} writer_args_t;

/* Adds a new line to the sensing data. */
static void append_line(const char *path, const char *text) {

    if (xSemaphoreTake(spi_flash_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW("HEARTBEAT", "Could not take lock to read %s", path);
        return;
    }
    ESP_LOGW(TAG, "Acquired lock. -> Append Line.");

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "append open failed: %s", path);
        xSemaphoreGive(spi_flash_lock);
        return;
    }
    unsigned long ts = (unsigned long) esp_log_timestamp();
    fprintf(f, "%lu, %s\n", ts, text ? text : "Test entry.");
    fflush(f);
    fclose(f);

    ESP_LOGW(TAG, "Released lock. -> Append Line.");
    xSemaphoreGive(spi_flash_lock);
}

static void writer_task_func(void *arg) {
    writer_args_t a = *(writer_args_t *)arg;
    free(arg); /* Debugged: struct copied args need to be freed.*/

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(a.interval_ms));
        append_line(a.path, a.line[0] ? a.line : "Test line.");
        ESP_LOGI(TAG, "writer: appended to %s", a.path);
    }
}

/* Function Calls. */
esp_err_t heartbeat_start(const char *csv_path, gpio_num_t pin, int period_ms) {
    if (!csv_path || period_ms <= 0){
        return ESP_ERR_INVALID_ARG;
    }
    if (heartbeat_task){
        return ESP_OK;
    }

    /* Update set up given by developer inputs OR default. */
    sensing_data_csv = csv_path;
    s_gpio_pin = pin;
    heartbeat_freq = period_ms;

    BaseType_t ok = xTaskCreate(heartbeat_task_func, "heartbeat_task", 4096, NULL, 5, &heartbeat_task);

    if (ok != pdPASS){
        return ESP_FAIL;
    } 
    
    return ESP_OK;
}

void heartbeat_set_period_ms(int period_ms) {
    if (period_ms > 0){
        heartbeat_freq = period_ms;
    } else {
        ESP_LOGW(TAG, "Heartbeat Frequency entered < 0. Using Default Frequency.");
    }
}

void heartbeat_stop(void) {
    if (!heartbeat_task){
        return;
    }
    vTaskDelete(heartbeat_task);
    heartbeat_task = NULL;
}

/* Heartbeat Testing Function Calls. */

esp_err_t test_writer_start(const char *csv_path, int interval_ms, const char *line_text) {
    if (!csv_path || interval_ms <= 0){
        return ESP_ERR_INVALID_ARG;
    }
    if (writer_task){
        return ESP_OK;
    }

    writer_args_t *args = (writer_args_t *) calloc(1, sizeof(writer_args_t));
    if (!args){
        return ESP_ERR_NO_MEM;
    }
    strncpy(args->path, csv_path, sizeof(args->path)-1);
    args->interval_ms = interval_ms;
    if (line_text){
        strncpy(args->line, line_text, sizeof(args->line)-1);
    }

    BaseType_t ok = xTaskCreate(writer_task_func, "test_writer", 4096, args, 5, &writer_task);
    if (ok != pdPASS){ 
        free(args); 
        return ESP_FAIL;
    }
    return ESP_OK;
}

void test_writer_stop(void) {
    if (!writer_task){
        return;
    }
    vTaskDelete(writer_task);
    writer_task = NULL;
}
#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

/*
* Start Heartbeat Task. 
* Reads sensing file periodically to see if new lines were added (indicating increase in data).
*/
esp_err_t heartbeat_start(const char *csv_path, gpio_num_t pin, int period_ms);

/*
* Start Heartbeat Task. 
* Sets the frequency at which the task checks the sensing csv for new data. 
*/
void heartbeat_set_period_ms(int period_ms);

/*
* Stops Heartbeat Task. 
*/
void heartbeat_stop(void);

/*
* Testing: Starts the task that adds a new line to the csv to mimic data written to the file in the real world.
*/
esp_err_t test_writer_start(const char *csv_path, int interval_ms, const char *line_text);

/*
* Testing: Stops data appending task.
*/
void test_writer_stop(void);
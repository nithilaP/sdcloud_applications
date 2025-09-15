#pragma once
#include "esp_err.h"

/* 
* A task for periodic compression of sensing data csv.
*/
esp_err_t compression_start(const char *input_csv_path, const char *output_csv_path, int interval_ms,const char *algo);

/* 
* Developer can set which compression algorithm to use on their data.
*/
void compression_set_algorithm(const char *algo);

/* 
* Developers can set the interval of their compression (frequency).
*/
void compression_set_interval(int interval_ms);

/* 
* Stop the periodic compression task.
*/
void compression_stop(void);

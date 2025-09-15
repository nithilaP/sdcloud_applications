#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start periodic compression of input_csv_path -> output_csv_path.
// algo: "rle" or "delta" (defaults to "rle" if NULL/unknown)
esp_err_t compression_start(const char *input_csv_path,
                            const char *output_csv_path,
                            int interval_ms,
                            const char *algo);

// Change algorithm at runtime: "rle" or "delta"
void compression_set_algorithm(const char *algo);

// Change interval (ms) at runtime
void compression_set_interval(int interval_ms);

// Stop the compression task
void compression_stop(void);

#ifdef __cplusplus
}
#endif

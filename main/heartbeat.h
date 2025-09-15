#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start heartbeat task: poll `csv_path` every `period_ms`, pulse `pin` if line-count increases. */
esp_err_t heartbeat_start(const char *csv_path, gpio_num_t pin, int period_ms);

/** Change the heartbeat period at runtime (ms). */
void heartbeat_set_period_ms(int period_ms);

/** Stop the heartbeat task (safe to call if not running). */
void heartbeat_stop(void);

/** Start a test-writer that appends a line to `csv_path` every `interval_ms`.
 *  If `line_text` is NULL, uses "Test entry." */
esp_err_t test_writer_start(const char *csv_path, int interval_ms, const char *line_text);

/** Stop the test-writer task (safe to call if not running). */
void test_writer_stop(void);

#ifdef __cplusplus
}
#endif

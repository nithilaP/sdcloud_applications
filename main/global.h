#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Declare the global lock (not define)
extern SemaphoreHandle_t spi_flash_lock;

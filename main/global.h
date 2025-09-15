#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* 
* Global Semaphore used to synchronizing access to SPI Flash.
*/
extern SemaphoreHandle_t spi_flash_lock;

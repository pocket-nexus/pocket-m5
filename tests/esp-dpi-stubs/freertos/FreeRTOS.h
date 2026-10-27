#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
TickType_t xTaskGetTickCount(void);

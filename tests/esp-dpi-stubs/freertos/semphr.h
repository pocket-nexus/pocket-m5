#pragma once
#include "FreeRTOS.h"
typedef struct {
  unsigned unused;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore,
                                 BaseType_t *woken);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);

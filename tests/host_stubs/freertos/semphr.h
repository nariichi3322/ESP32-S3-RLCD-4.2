#pragma once

#include "FreeRTOS.h"

#include <mutex>

struct StaticSemaphore_t {
    std::mutex mutex;
};
using SemaphoreHandle_t = StaticSemaphore_t *;

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    return storage;
}

inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t)
{
    semaphore->mutex.lock();
    return pdTRUE;
}

inline int xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    semaphore->mutex.unlock();
    return pdTRUE;
}


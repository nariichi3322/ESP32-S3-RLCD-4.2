// 为普通任务上下文提供 FreeRTOS semaphore/mutex 的作用域释放保护。
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class ScopedSemaphoreLock {
public:
    explicit ScopedSemaphoreLock(SemaphoreHandle_t semaphore,
                                 TickType_t wait_ticks = portMAX_DELAY)
        : semaphore_(semaphore),
          locked_(semaphore_ &&
                  xSemaphoreTake(semaphore_, wait_ticks) == pdTRUE)
    {
    }

    ~ScopedSemaphoreLock()
    {
        if (locked_) {
            xSemaphoreGive(semaphore_);
        }
    }

    explicit operator bool() const
    {
        return locked_;
    }

    ScopedSemaphoreLock(const ScopedSemaphoreLock &) = delete;
    ScopedSemaphoreLock &operator=(const ScopedSemaphoreLock &) = delete;

private:
    SemaphoreHandle_t semaphore_;
    bool locked_;
};

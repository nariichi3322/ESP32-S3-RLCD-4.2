// 为联网任务提供可重复释放的唤醒锁和临时 HTTP 超时作用域守卫。
#pragma once

#include "app_state.h"
#include "sensor_services.h"

class NetworkAwakeLockGuard {
public:
    NetworkAwakeLockGuard()
    {
        acquire_network_awake_lock();
    }

    ~NetworkAwakeLockGuard()
    {
        release();
    }

    NetworkAwakeLockGuard(const NetworkAwakeLockGuard &) = delete;
    NetworkAwakeLockGuard &operator=(const NetworkAwakeLockGuard &) = delete;

    void release()
    {
        if (active_) {
            release_network_awake_lock();
            active_ = false;
        }
    }

private:
    bool active_ = true;
};

class NetworkHttpTimeoutGuard {
public:
    explicit NetworkHttpTimeoutGuard(int timeout_ms)
        : previous_timeout_ms_(g_http_timeout_ms)
    {
        g_http_timeout_ms = timeout_ms;
    }

    ~NetworkHttpTimeoutGuard()
    {
        g_http_timeout_ms = previous_timeout_ms_;
    }

    NetworkHttpTimeoutGuard(const NetworkHttpTimeoutGuard &) = delete;
    NetworkHttpTimeoutGuard &operator=(const NetworkHttpTimeoutGuard &) = delete;

private:
    int previous_timeout_ms_ = 0;
};

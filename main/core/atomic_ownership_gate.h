// 提供跨任务单所有者资源的轻量原子申请、释放和状态查询。
#pragma once

#include <atomic>

class AtomicOwnershipGate {
public:
    bool try_acquire()
    {
        bool expected = false;
        return active_.compare_exchange_strong(expected,
                                               true,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
    }

    void release()
    {
        active_.store(false, std::memory_order_release);
    }

    bool active() const
    {
        return active_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> active_{false};
};

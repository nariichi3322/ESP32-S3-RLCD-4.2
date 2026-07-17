// 验证通用原子所有权门及延后任务兼容类型都只允许一个并发所有者。
#include "atomic_ownership_gate.h"
#include "single_pending_task_gate.h"

#include <assert.h>
#include <atomic>
#include <thread>
#include <vector>

template <typename Gate>
void test_gate()
{
    Gate gate;
    assert(!gate.active());
    assert(gate.try_acquire());
    assert(gate.active());
    assert(!gate.try_acquire());
    assert(gate.active());

    gate.release();
    assert(!gate.active());
    assert(gate.try_acquire());
    gate.release();
    assert(!gate.active());

    constexpr int kContenders = 16;
    std::atomic<bool> start{false};
    std::atomic<int> winners{0};
    std::vector<std::thread> contenders;
    contenders.reserve(kContenders);
    for (int i = 0; i < kContenders; ++i) {
        contenders.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
            }
            if (gate.try_acquire()) {
                winners.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &contender : contenders) {
        contender.join();
    }
    assert(winners.load(std::memory_order_relaxed) == 1);
    assert(gate.active());
    gate.release();
}

int main()
{
    test_gate<AtomicOwnershipGate>();
    test_gate<SinglePendingTaskGate>();
    return 0;
}

// 验证闹钟复合快照、原子启用镜像和覆盖确认始终保持一致状态。
#include "alarm_runtime_state.h"

#include <assert.h>
#include <atomic>
#include <thread>

namespace {
bool is_state_a(const AlarmSnapshot &snapshot)
{
    return !snapshot.enabled && snapshot.ringing &&
           snapshot.hour == 6 && snapshot.minute == 30;
}

bool is_state_b(const AlarmSnapshot &snapshot)
{
    return snapshot.enabled && !snapshot.ringing &&
           snapshot.hour == 7 && snapshot.minute == 45;
}
}

int main()
{
    static_assert(sizeof(AlarmSnapshot) == 4,
                  "alarm snapshot should contain only visible runtime fields");
    AlarmSnapshot snapshot = {};
    assert(!alarm_runtime_snapshot(&snapshot));
    assert(!alarm_runtime_publish(true, false, 6, 30));
    assert(!alarm_runtime_clear_replacement());
    assert(!alarm_runtime_is_enabled());

    assert(alarm_runtime_state_init());
    assert(alarm_runtime_state_init());
    assert(alarm_runtime_snapshot(&snapshot));
    assert(!snapshot.enabled && !snapshot.ringing);
    assert(snapshot.hour == 0 && snapshot.minute == 0);

    assert(alarm_runtime_publish(true, false, 6, 30));
    assert(alarm_runtime_is_enabled());
    AlarmSnapshot existing = {};
    assert(alarm_runtime_replacement_decision(7, 45, false, 1000, 120000, &existing) ==
           kAlarmReplacementConfirmationRequired);
    assert(existing.enabled && existing.hour == 6 && existing.minute == 30);
    assert(alarm_runtime_replacement_decision(7, 45, true, 1001, 120000, &existing) ==
           kAlarmReplacementAccepted);

    assert(alarm_runtime_replacement_decision(8, 15, false, 2000, 120000, nullptr) ==
           kAlarmReplacementConfirmationRequired);
    assert(alarm_runtime_publish(true, false, 9, 5));
    assert(alarm_runtime_replacement_decision(8, 15, true, 2001, 120000, nullptr) ==
           kAlarmReplacementConfirmationInvalid);
    assert(alarm_runtime_clear_replacement());

    assert(alarm_runtime_publish(false, true, 6, 30));
    std::atomic<bool> writer_done{false};
    std::thread writer([&]() {
        for (int i = 0; i < 10000; ++i) {
            if (i & 1) {
                assert(alarm_runtime_publish(true, false, 7, 45));
            } else {
                assert(alarm_runtime_publish(false, true, 6, 30));
            }
        }
        writer_done.store(true, std::memory_order_release);
    });
    do {
        assert(alarm_runtime_snapshot(&snapshot));
        assert(is_state_a(snapshot) || is_state_b(snapshot));
    } while (!writer_done.load(std::memory_order_acquire));
    writer.join();

    assert(alarm_runtime_snapshot(&snapshot));
    assert(is_state_b(snapshot));
    assert(alarm_runtime_is_enabled());
    return 0;
}

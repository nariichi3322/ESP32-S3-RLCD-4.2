// 验证每日文字与同步时间在并发读写中始终来自同一发布批次。
#include "daily_saying_state.h"

#include "daily_saying_contract.h"

#include <assert.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>

int main()
{
    assert(daily_saying_state_version_load() == 0);
    assert(daily_saying_state_init());
    assert(daily_saying_state_init());
    load_daily_saying_cache();
    assert(daily_saying_state_version_load() == 1);

    char text[kDailySayingLen] = {};
    time_t synced_at = 99;
    assert(!get_daily_saying_snapshot(text, sizeof(text), &synced_at));
    assert(text[0] == '\0');
    assert(synced_at == 0);
    assert(!get_daily_saying_snapshot(nullptr, sizeof(text), &synced_at));
    assert(!get_daily_saying_snapshot(text, 0, &synced_at));
    DailySayingCacheSnapshot cache = {true, 99, 99};
    assert(daily_saying_cache_snapshot_load(&cache));
    assert(!cache.available);
    assert(cache.last_sync_time == 0);
    assert(cache.version == 1);
    assert(!daily_saying_cache_snapshot_load(nullptr));

    assert(daily_saying_state_publish("今日宜保持耐心", 1234));
    assert(daily_saying_state_version_load() == 2);
    assert(get_daily_saying_snapshot(text, sizeof(text), &synced_at));
    assert(strcmp(text, "今日宜保持耐心") == 0);
    assert(synced_at == 1234);
    assert(daily_saying_cache_snapshot_load(&cache));
    assert(cache.available);
    assert(cache.last_sync_time == 1234);
    assert(cache.version == 2);

    char short_text[5] = {};
    assert(get_daily_saying_snapshot(short_text, sizeof(short_text), nullptr));
    assert(strlen(short_text) == sizeof(short_text) - 1);

    char oversized_output[kDailySayingLen + 64] = {};
    assert(get_daily_saying_snapshot(oversized_output,
                                     sizeof(oversized_output),
                                     nullptr));
    assert(strcmp(oversized_output, "今日宜保持耐心") == 0);

    char long_text[kDailySayingLen + 32];
    memset(long_text, 'x', sizeof(long_text));
    long_text[sizeof(long_text) - 1] = '\0';
    assert(daily_saying_state_publish(long_text, 5678));
    assert(daily_saying_state_version_load() == 3);
    assert(get_daily_saying_snapshot(text, sizeof(text), &synced_at));
    assert(strlen(text) == kDailySayingLen - 1);
    assert(synced_at == 5678);

    constexpr const char *kTextA = "A-每日文字批次";
    constexpr const char *kTextB = "B-每日文字批次";
    constexpr time_t kTimeA = 101;
    constexpr time_t kTimeB = 202;
    assert(daily_saying_state_publish(kTextA, kTimeA));
    uint32_t last_version = daily_saying_state_version_load();
    std::atomic<bool> writer_done{false};
    std::thread writer([&writer_done]() {
        for (int iteration = 0; iteration < 10000; ++iteration) {
            const bool use_a = (iteration & 1) == 0;
            assert(daily_saying_state_publish(use_a ? kTextA : kTextB,
                                              use_a ? kTimeA : kTimeB));
        }
        writer_done.store(true, std::memory_order_release);
    });
    do {
        assert(get_daily_saying_snapshot(text, sizeof(text), &synced_at));
        const bool is_a = strcmp(text, kTextA) == 0 && synced_at == kTimeA;
        const bool is_b = strcmp(text, kTextB) == 0 && synced_at == kTimeB;
        assert(is_a || is_b);
        assert(daily_saying_cache_snapshot_load(&cache));
        assert(cache.available);
        assert(cache.last_sync_time == kTimeA || cache.last_sync_time == kTimeB);
        assert(cache.version >= last_version);
        const uint32_t current_version = daily_saying_state_version_load();
        assert(cache.version <= current_version);
        assert(current_version >= last_version);
        last_version = current_version;
    } while (!writer_done.load(std::memory_order_acquire));
    writer.join();

    assert(daily_saying_state_publish(nullptr, 0));
    assert(daily_saying_state_version_load() > last_version);
    assert(!get_daily_saying_snapshot(text, sizeof(text), &synced_at));
    assert(text[0] == '\0');
    assert(synced_at == 0);
    assert(daily_saying_cache_snapshot_load(&cache));
    assert(!cache.available);
    assert(cache.last_sync_time == 0);
    assert(cache.version == daily_saying_state_version_load());
    return 0;
}

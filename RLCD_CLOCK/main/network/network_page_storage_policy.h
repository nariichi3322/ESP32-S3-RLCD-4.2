// 定義工作頁 NVS 版本遷移使用的純計算規則。
#pragma once

#include "work_page_ids.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace network_page_storage {

inline constexpr size_t kLegacyV4WorkPageCount = kWorkPageHistory + 1;
inline constexpr size_t kLegacyV5WorkPageCount = kWorkPageXiaozhiAI + 1;
inline constexpr size_t kLegacyV6WorkPageCount = kWorkPageAggregateClock + 1;

inline constexpr WorkPageMask kLegacyV4KnownPageMask =
    static_cast<WorkPageMask>((1U << kLegacyV4WorkPageCount) - 1U);
inline constexpr WorkPageMask kLegacyV5KnownPageMask =
    static_cast<WorkPageMask>((1U << kLegacyV5WorkPageCount) - 1U);
inline constexpr WorkPageMask kLegacyV6KnownPageMask =
    static_cast<WorkPageMask>((1U << kLegacyV6WorkPageCount) - 1U);
inline constexpr WorkPageMask kCurrentKnownPageMask =
    static_cast<WorkPageMask>((1U << kWorkPageCount) - 1U);

constexpr WorkPageMask migrate_v4_page_mask(uint8_t legacy_mask)
{
    return static_cast<WorkPageMask>(legacy_mask) |
           work_page_mask_bit(kWorkPageXiaozhiAI) |
           work_page_mask_bit(kWorkPageAggregateClock) |
           work_page_mask_bit(kWorkPageCodexUsage);
}

constexpr WorkPageMask migrate_v5_page_mask(uint8_t legacy_mask)
{
    return static_cast<WorkPageMask>(legacy_mask) |
           work_page_mask_bit(kWorkPageAggregateClock) |
           work_page_mask_bit(kWorkPageCodexUsage);
}

// V6 intentionally follows main's definition: ID 7 is aggregate clock and
// Codex is appended as ID 8. No source or provider heuristic is allowed.
constexpr WorkPageMask migrate_v6_page_mask(uint8_t legacy_mask)
{
    return static_cast<WorkPageMask>(legacy_mask) |
           work_page_mask_bit(kWorkPageCodexUsage);
}

template <size_t N>
constexpr bool legacy_page_order_is_valid(const uint8_t (&order)[N], size_t page_count)
{
    if (N != page_count) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        if (order[i] >= page_count) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (order[i] == order[j]) {
                return false;
            }
        }
    }
    return true;
}

inline bool migrate_v4_page_order(const uint8_t *legacy_order,
                                   size_t legacy_order_size,
                                   uint8_t *current_order,
                                   size_t current_order_size)
{
    if (!legacy_order || legacy_order_size != kLegacyV4WorkPageCount ||
        !current_order || current_order_size != kWorkPageCount) {
        return false;
    }
    uint8_t legacy[kLegacyV4WorkPageCount] = {};
    memcpy(legacy, legacy_order, sizeof(legacy));
    if (!legacy_page_order_is_valid(legacy, kLegacyV4WorkPageCount)) {
        return false;
    }
    memcpy(current_order, legacy, sizeof(legacy));
    current_order[kWorkPageXiaozhiAI] = kWorkPageXiaozhiAI;
    current_order[kWorkPageAggregateClock] = kWorkPageAggregateClock;
    current_order[kWorkPageCodexUsage] = kWorkPageCodexUsage;
    return true;
}

inline bool migrate_v5_page_order(const uint8_t *legacy_order,
                                   size_t legacy_order_size,
                                   uint8_t *current_order,
                                   size_t current_order_size)
{
    if (!legacy_order || legacy_order_size != kLegacyV5WorkPageCount ||
        !current_order || current_order_size != kWorkPageCount) {
        return false;
    }
    uint8_t legacy[kLegacyV5WorkPageCount] = {};
    memcpy(legacy, legacy_order, sizeof(legacy));
    if (!legacy_page_order_is_valid(legacy, kLegacyV5WorkPageCount)) {
        return false;
    }
    memcpy(current_order, legacy, sizeof(legacy));
    current_order[kWorkPageAggregateClock] = kWorkPageAggregateClock;
    current_order[kWorkPageCodexUsage] = kWorkPageCodexUsage;
    return true;
}

inline bool migrate_v6_page_order(const uint8_t *legacy_order,
                                   size_t legacy_order_size,
                                   uint8_t *current_order,
                                   size_t current_order_size)
{
    if (!legacy_order || legacy_order_size != kLegacyV6WorkPageCount ||
        !current_order || current_order_size != kWorkPageCount) {
        return false;
    }
    uint8_t legacy[kLegacyV6WorkPageCount] = {};
    memcpy(legacy, legacy_order, sizeof(legacy));
    if (!legacy_page_order_is_valid(legacy, kLegacyV6WorkPageCount)) {
        return false;
    }
    memcpy(current_order, legacy, sizeof(legacy));
    current_order[kWorkPageCodexUsage] = kWorkPageCodexUsage;
    return true;
}

static_assert(kWorkPageXiaozhiAI == static_cast<int>(kLegacyV4WorkPageCount),
              "v4 migration expects Xiaozhi AI after legacy pages");
static_assert(kWorkPageAggregateClock == static_cast<int>(kLegacyV5WorkPageCount),
              "v5 migration expects aggregate clock after legacy pages");
static_assert(kWorkPageCodexUsage == static_cast<int>(kLegacyV6WorkPageCount),
              "v6 migration expects Codex after main V6 pages");
static_assert((kLegacyV4KnownPageMask & work_page_mask_bit(kWorkPageXiaozhiAI)) == 0,
              "v4 mask must not contain the Xiaozhi AI page");

} // namespace network_page_storage

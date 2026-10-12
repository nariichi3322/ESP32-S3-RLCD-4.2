// 验证工作页 v4 到 v5 的 mask 与顺序迁移规则。
#include "network_page_storage_policy.h"

#include <cassert>
#include <cstring>

int main()
{
    static_assert(network_page_storage::kLegacyV4KnownPageMask == 0x3f);
    static_assert(network_page_storage::kLegacyV6KnownPageMask == 0xff);
    static_assert(network_page_storage::kLegacyV7KnownPageMask == 0x1ff);
    static_assert(network_page_storage::kCurrentKnownPageMask == 0x3ff);
    static_assert(network_page_storage::migrate_v4_page_mask(0x00) == 0x3c0);
    static_assert(network_page_storage::migrate_v4_page_mask(0x3f) == 0x3ff);
    static_assert(network_page_storage::migrate_v5_page_mask(0x00) == 0x380);
    static_assert(network_page_storage::migrate_v5_page_mask(0x7f) == 0x3ff);
    static_assert(network_page_storage::migrate_v6_page_mask(0x00) == 0x300);
    static_assert(network_page_storage::migrate_v6_page_mask(0xff) == 0x3ff);
    static_assert(network_page_storage::migrate_v7_page_mask(0x000) == 0x200);
    static_assert(network_page_storage::migrate_v7_page_mask(0x1ff) == 0x3ff);

    const uint8_t legacy_order[] = {2, 0, 1, 3, 4, 5};
    const uint8_t expected_order[] = {2, 0, 1, 3, 4, 5, 6, 7, 8, 9};
    uint8_t current_order[kWorkPageCount] = {};
    assert(network_page_storage::migrate_v4_page_order(legacy_order,
                                                       sizeof(legacy_order),
                                                       current_order,
                                                       sizeof(current_order)));
    assert(memcmp(current_order, expected_order, sizeof(expected_order)) == 0);

    uint8_t untouched[kWorkPageCount] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
    assert(!network_page_storage::migrate_v4_page_order(nullptr,
                                                        sizeof(legacy_order),
                                                        untouched,
                                                        sizeof(untouched)));
    assert(!network_page_storage::migrate_v4_page_order(legacy_order,
                                                        sizeof(legacy_order) - 1,
                                                        untouched,
                                                        sizeof(untouched)));
    assert(!network_page_storage::migrate_v4_page_order(legacy_order,
                                                        sizeof(legacy_order),
                                                        untouched,
                                                        sizeof(untouched) - 1));
    for (uint8_t value : untouched) {
        assert(value == 99);
    }
    const uint8_t v5_order[] = {6, 2, 0, 1, 3, 4, 5};
    assert(network_page_storage::migrate_v5_page_order(v5_order,
                                                       sizeof(v5_order),
                                                       current_order,
                                                       sizeof(current_order)));
    assert(current_order[7] == kWorkPageAggregateClock);
    assert(current_order[8] == kWorkPageCodexUsage);
    assert(current_order[9] == kWorkPageClaudeUsage);

    const uint8_t v6_order[] = {7, 2, 0, 1, 3, 4, 5, 6};
    assert(network_page_storage::migrate_v6_page_order(
        v6_order, sizeof(v6_order), current_order, sizeof(current_order)));
    assert(current_order[0] == kWorkPageAggregateClock);
    assert(current_order[8] == kWorkPageCodexUsage);
    assert(current_order[9] == kWorkPageClaudeUsage);
    const uint8_t malformed_v6[] = {0, 0, 1, 2, 3, 4, 5, 6};
    assert(!network_page_storage::migrate_v6_page_order(
        malformed_v6, sizeof(malformed_v6), current_order, sizeof(current_order)));

    const uint8_t v7_order[] = {8, 7, 2, 0, 1, 3, 4, 5, 6};
    assert(network_page_storage::migrate_v7_page_order(
        v7_order, sizeof(v7_order), current_order, sizeof(current_order)));
    assert(memcmp(current_order, v7_order, sizeof(v7_order)) == 0);
    assert(current_order[9] == kWorkPageClaudeUsage);
    const uint8_t malformed_v7[] = {8, 8, 2, 0, 1, 3, 4, 5, 6};
    assert(!network_page_storage::migrate_v7_page_order(
        malformed_v7, sizeof(malformed_v7), current_order, sizeof(current_order)));
    assert(!network_page_storage::migrate_v7_page_order(
        v7_order, sizeof(v7_order) - 1, current_order, sizeof(current_order)));
    return 0;
}

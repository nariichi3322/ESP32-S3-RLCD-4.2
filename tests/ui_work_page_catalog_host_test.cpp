// 验证工作页名称、启用掩码、离线筛选和自定义顺序规则。
#include "ui_work_page_catalog.h"

#include "app_state.h"

#include <assert.h>
#include <string.h>

volatile int g_active_work_page = kWorkPageWeatherClock;
uint8_t g_work_page_enabled_mask = 0;
uint8_t g_work_page_order[kWorkPageCount] = {};

namespace {

constexpr uint8_t page_bit(int page)
{
    return static_cast<uint8_t>(1U << page);
}

void expect_default_order()
{
    const uint8_t expected[kWorkPageCount] = {
        kWorkPageWeatherClock,
        kWorkPageGallery,
        kWorkPageWeatherBoard,
        kWorkPageFlipClock,
        kWorkPageCalendar,
        kWorkPageHistory,
        kWorkPageXiaozhiAI,
    };
    assert(memcmp(g_work_page_order, expected, sizeof(expected)) == 0);
}

} // namespace

int main()
{
    reset_work_page_order();
    expect_default_order();
    g_work_page_enabled_mask = static_cast<uint8_t>((1U << kWorkPageCount) - 1U);

    assert(strcmp(work_page_name(kWorkPageWeatherClock), "天气时钟") == 0);
    assert(strcmp(work_page_name(kWorkPageHistory), "温湿历史") == 0);
    assert(strcmp(work_page_name(kWorkPageXiaozhiAI), "小智AI") == 0);
    assert(strcmp(work_page_name(-1), "未知页面") == 0);
    assert(display_settings_item_work_page(0) == kWorkPageWeatherClock);
    assert(display_settings_item_work_page(kWorkPageCount - 1) == kWorkPageXiaozhiAI);
    assert(display_settings_item_work_page(-1) == -1);
    assert(display_settings_item_work_page(kWorkPageCount) == -1);

    assert(work_page_requires_network(kWorkPageWeatherClock));
    assert(work_page_requires_network(kWorkPageGallery));
    assert(work_page_requires_network(kWorkPageWeatherBoard));
    assert(work_page_requires_network(kWorkPageXiaozhiAI));
    assert(!work_page_requires_network(kWorkPageFlipClock));
    assert(!work_page_requires_network(kWorkPageCalendar));
    assert(!work_page_requires_network(kWorkPageHistory));
    assert(!work_page_requires_network(-1));

    assert(!work_page_uses_low_refresh_idle(kWorkPageWeatherClock));
    assert(work_page_uses_low_refresh_idle(kWorkPageGallery));
    assert(work_page_uses_low_refresh_idle(kWorkPageWeatherBoard));
    assert(!work_page_uses_low_refresh_idle(kWorkPageFlipClock));
    assert(work_page_uses_low_refresh_idle(kWorkPageCalendar));
    assert(work_page_uses_low_refresh_idle(kWorkPageHistory));
    assert(!work_page_uses_low_refresh_idle(kWorkPageXiaozhiAI));
    assert(!work_page_uses_low_refresh_idle(-1));

    const uint8_t all_pages = static_cast<uint8_t>((1U << kWorkPageCount) - 1U);
    assert(normalize_work_page_enabled_mask(all_pages) == all_pages);
    assert(normalize_work_page_enabled_mask(0) == all_pages);
    assert(normalize_work_page_enabled_mask(0x80) == all_pages);
    assert(normalize_work_page_enabled_mask(page_bit(kWorkPageXiaozhiAI)) ==
           (page_bit(kWorkPageWeatherClock) | page_bit(kWorkPageXiaozhiAI)));
    assert(normalize_work_page_enabled_mask(page_bit(kWorkPageCalendar) |
                                            page_bit(kWorkPageXiaozhiAI)) ==
           (page_bit(kWorkPageCalendar) | page_bit(kWorkPageXiaozhiAI)));

    const uint8_t local_pages = page_bit(kWorkPageFlipClock) |
                                page_bit(kWorkPageCalendar);
    assert(work_page_mask_for_offline_mode(g_work_page_enabled_mask) ==
           (page_bit(kWorkPageFlipClock) |
            page_bit(kWorkPageCalendar) |
            page_bit(kWorkPageHistory)));
    assert(work_page_mask_for_offline_mode(local_pages |
                                           page_bit(kWorkPageWeatherClock)) ==
           local_pages);
    assert(work_page_mask_for_offline_mode(page_bit(kWorkPageWeatherClock)) ==
           page_bit(kWorkPageFlipClock));

    g_work_page_order[0] = kWorkPageXiaozhiAI;
    g_work_page_order[1] = kWorkPageHistory;
    g_work_page_order[2] = kWorkPageCalendar;
    g_work_page_order[3] = kWorkPageFlipClock;
    g_work_page_order[4] = kWorkPageWeatherBoard;
    g_work_page_order[5] = kWorkPageGallery;
    g_work_page_order[6] = kWorkPageWeatherClock;
    normalize_work_page_order();
    assert(g_work_page_order[0] == kWorkPageHistory);
    assert(g_work_page_order[1] == kWorkPageXiaozhiAI);
    assert(first_enabled_work_page() == kWorkPageHistory);
    assert(work_page_order_has_valid_home());

    g_work_page_enabled_mask = page_bit(kWorkPageXiaozhiAI);
    normalize_work_page_order();
    assert(!work_page_mask_has_valid_home(g_work_page_enabled_mask));
    assert(!work_page_order_has_valid_home());

    g_work_page_enabled_mask = page_bit(kWorkPageWeatherClock) |
                               page_bit(kWorkPageCalendar) |
                               page_bit(kWorkPageHistory);
    reset_work_page_order();
    assert(first_enabled_work_page() == kWorkPageWeatherClock);
    assert(next_enabled_work_page(kWorkPageWeatherClock) == kWorkPageCalendar);
    assert(next_enabled_work_page(kWorkPageCalendar) == kWorkPageHistory);
    assert(next_enabled_work_page(kWorkPageHistory) == kWorkPageWeatherClock);

    g_active_work_page = kWorkPageGallery;
    ensure_active_work_page_enabled();
    assert(g_active_work_page == kWorkPageWeatherClock);

    g_work_page_order[0] = kWorkPageWeatherClock;
    g_work_page_order[1] = kWorkPageWeatherClock;
    normalize_work_page_order();
    expect_default_order();
    return 0;
}

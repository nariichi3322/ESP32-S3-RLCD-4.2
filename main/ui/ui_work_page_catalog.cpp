// 管理工作页名称、启用状态、设置映射和用户自定义顺序。
#include "ui_work_page_catalog.h"

#include "app_constexpr.h"
#include "app_state.h"

namespace {
constexpr int kFirstWorkPage = kWorkPageWeatherClock;
constexpr int kFallbackWorkPage = kWorkPageWeatherClock;
constexpr int kInvalidWorkPage = -1;
constexpr int kInvalidWorkPageOrderIndex = -1;
constexpr uint8_t kDefaultWorkPageOrder[kWorkPageCount] = {
    kWorkPageWeatherClock,
    kWorkPageGallery,
    kWorkPageWeatherBoard,
    kWorkPageFlipClock,
    kWorkPageCalendar,
    kWorkPageHistory,
    kWorkPageXiaozhiAI,
};
constexpr int kDisplaySettingPages[kDisplaySettingsPageItemCount] = {
    kWorkPageWeatherClock,
    kWorkPageGallery,
    kWorkPageWeatherBoard,
    kWorkPageFlipClock,
    kWorkPageCalendar,
    kWorkPageHistory,
    kWorkPageXiaozhiAI,
};
constexpr const char *kWorkPageNames[kWorkPageCount] = {
    "天气时钟",
    "图片时钟",
    "天气看板",
    "温湿时钟",
    "日历",
    "温湿历史",
    "小智AI",
};
constexpr const char *kUnknownWorkPageName = "未知页面";

bool is_work_page_index(int page)
{
    return page >= kFirstWorkPage && page < kWorkPageCount;
}

bool is_work_page_order_index(int index)
{
    return index >= 0 && index < kWorkPageCount;
}

bool work_page_order_index_found(int index)
{
    return index != kInvalidWorkPageOrderIndex;
}

constexpr uint8_t work_page_mask(int page)
{
    return static_cast<uint8_t>(1U << page);
}

constexpr uint8_t kAllWorkPageMask = static_cast<uint8_t>((1U << kWorkPageCount) - 1U);
constexpr uint8_t kNetworkWorkPageMask = work_page_mask(kWorkPageWeatherClock) |
                                         work_page_mask(kWorkPageGallery) |
                                         work_page_mask(kWorkPageWeatherBoard) |
                                         work_page_mask(kWorkPageXiaozhiAI);
constexpr uint8_t kLowRefreshIdleWorkPageMask = work_page_mask(kWorkPageGallery) |
                                                work_page_mask(kWorkPageWeatherBoard) |
                                                work_page_mask(kWorkPageCalendar) |
                                                work_page_mask(kWorkPageHistory);
constexpr uint8_t kLocalWorkPageMask = static_cast<uint8_t>(~kNetworkWorkPageMask) & kAllWorkPageMask;

bool page_mask_has_non_xiaozhi_page(uint8_t page_mask)
{
    for (int page = kFirstWorkPage; page < kWorkPageCount; ++page) {
        if (page != kWorkPageXiaozhiAI && (page_mask & work_page_mask(page)) != 0) {
            return true;
        }
    }
    return false;
}

template <typename T, size_t N>
constexpr bool page_list_covers_each_work_page_once(const T (&pages)[N])
{
    if (N != kWorkPageCount) {
        return false;
    }
    for (int page = kFirstWorkPage; page < kWorkPageCount; ++page) {
        int hits = 0;
        for (size_t i = 0; i < N; ++i) {
            if (pages[i] == page) {
                ++hits;
            }
        }
        if (hits != 1) {
            return false;
        }
    }
    return true;
}

bool work_page_order_valid(const uint8_t *order, size_t order_size)
{
    if (!order || order_size != kWorkPageCount) {
        return false;
    }
    bool seen[kWorkPageCount] = {};
    for (int i = 0; i < kWorkPageCount; ++i) {
        uint8_t page = order[i];
        if (page >= kWorkPageCount || seen[page]) {
            return false;
        }
        seen[page] = true;
    }
    for (bool present : seen) {
        if (!present) {
            return false;
        }
    }
    return true;
}

int work_page_order_index_normalized(int page)
{
    for (int i = 0; i < kWorkPageCount; ++i) {
        if (g_work_page_order[i] == page) {
            return i;
        }
    }
    return kInvalidWorkPageOrderIndex;
}

int first_enabled_work_page_order_index_or_invalid_normalized()
{
    for (int i = 0; i < kWorkPageCount; ++i) {
        if (is_work_page_enabled(g_work_page_order[i])) {
            return i;
        }
    }
    return kInvalidWorkPageOrderIndex;
}

int next_enabled_work_page_order_index_or_invalid_normalized(int current_order_index)
{
    for (int step = 1; step <= kWorkPageCount; ++step) {
        int index = (current_order_index + step + kWorkPageCount) % kWorkPageCount;
        if (is_work_page_enabled(g_work_page_order[index])) {
            return index;
        }
    }
    return kInvalidWorkPageOrderIndex;
}

int first_enabled_work_page_order_index_normalized()
{
    int index = first_enabled_work_page_order_index_or_invalid_normalized();
    return work_page_order_index_found(index) ? index : 0;
}

int valid_enabled_work_page_order_index_normalized(int current_order_index)
{
    if (!is_work_page_order_index(current_order_index) ||
        !is_work_page_enabled(g_work_page_order[current_order_index])) {
        return first_enabled_work_page_order_index_normalized();
    }
    return current_order_index;
}

static_assert(kFirstWorkPage == 0, "work page ids must start at zero");
static_assert(kFallbackWorkPage == kWorkPageWeatherClock, "work page order fallback must remain weather clock");
static_assert(kWorkPageCount > 0, "there must be at least one work page");
static_assert(kWorkPageCount <= static_cast<int>(sizeof(uint8_t) * 8),
              "work page enabled mask is stored as uint8_t");
static_assert(kLocalWorkPageMask != 0, "offline mode requires at least one local work page");
static_assert((kLocalWorkPageMask & kNetworkWorkPageMask) == 0,
              "local and network work page masks must not overlap");
static_assert(kLowRefreshIdleWorkPageMask != 0,
              "low-refresh idle page mask must not be empty");
static_assert((kLowRefreshIdleWorkPageMask & ~kAllWorkPageMask) == 0,
              "low-refresh idle page mask must only contain work pages");
static_assert(array_count(kDefaultWorkPageOrder) == kWorkPageCount,
              "default work page order must cover every work page");
static_assert(array_count(kDisplaySettingPages) == kDisplaySettingsPageItemCount,
              "display setting page mapping must match the settings item count");
static_assert(array_count(kWorkPageNames) == kWorkPageCount,
              "work page names must cover every work page");
static_assert(cstr_array_nonempty(kWorkPageNames), "work page names must be non-empty");
static_assert(cstr_nonempty(kUnknownWorkPageName), "unknown work page name must be non-empty");
static_assert(page_list_covers_each_work_page_once(kDefaultWorkPageOrder),
              "default work page order must include every work page exactly once");
static_assert(page_list_covers_each_work_page_once(kDisplaySettingPages),
              "display settings must map every work page exactly once");
}

bool is_work_page_enabled(int page)
{
    if (!is_work_page_index(page)) {
        return false;
    }
    return (g_work_page_enabled_mask & work_page_mask(page)) != 0;
}

bool work_page_requires_network(int page)
{
    return is_work_page_index(page) && (kNetworkWorkPageMask & work_page_mask(page)) != 0;
}

bool work_page_uses_low_refresh_idle(int page)
{
    return is_work_page_index(page) &&
           (kLowRefreshIdleWorkPageMask & work_page_mask(page)) != 0;
}

uint8_t normalize_work_page_enabled_mask(uint8_t page_mask)
{
    page_mask &= kAllWorkPageMask;
    if (page_mask == 0) {
        return kAllWorkPageMask;
    }
    if (!page_mask_has_non_xiaozhi_page(page_mask)) {
        page_mask |= work_page_mask(kWorkPageWeatherClock);
    }
    return page_mask;
}

uint8_t work_page_mask_for_offline_mode(uint8_t page_mask)
{
    uint8_t local_mask = static_cast<uint8_t>(page_mask & kLocalWorkPageMask);
    if (local_mask != 0) {
        return local_mask;
    }
    if (work_page_order_valid(g_work_page_order, sizeof(g_work_page_order))) {
        for (uint8_t page : g_work_page_order) {
            if (!work_page_requires_network(page)) {
                return work_page_mask(page);
            }
        }
    }
    return work_page_mask(kWorkPageFlipClock);
}

const char *work_page_name(int page)
{
    if (!is_work_page_index(page)) {
        return kUnknownWorkPageName;
    }
    return kWorkPageNames[page];
}

int display_settings_item_work_page(int item)
{
    if (item < 0 || item >= kDisplaySettingsPageItemCount) {
        return kInvalidWorkPage;
    }
    return kDisplaySettingPages[item];
}

int first_enabled_work_page()
{
    normalize_work_page_order();
    int index = first_enabled_work_page_order_index_or_invalid_normalized();
    return work_page_order_index_found(index) ? g_work_page_order[index] : kFallbackWorkPage;
}

void reset_work_page_order()
{
    static_assert(sizeof(kDefaultWorkPageOrder) == sizeof(g_work_page_order),
                  "default work page order storage must match runtime order storage");
    memcpy(g_work_page_order, kDefaultWorkPageOrder, sizeof(g_work_page_order));
}

void normalize_work_page_order()
{
    if (!work_page_order_valid(g_work_page_order, sizeof(g_work_page_order))) {
        reset_work_page_order();
    }
    int first_enabled = first_enabled_work_page_order_index_or_invalid_normalized();
    if (!work_page_order_index_found(first_enabled) ||
        g_work_page_order[first_enabled] != kWorkPageXiaozhiAI) {
        return;
    }
    for (int i = first_enabled + 1; i < kWorkPageCount; ++i) {
        if (is_work_page_enabled(g_work_page_order[i]) &&
            g_work_page_order[i] != kWorkPageXiaozhiAI) {
            uint8_t replacement = g_work_page_order[i];
            g_work_page_order[i] = g_work_page_order[first_enabled];
            g_work_page_order[first_enabled] = replacement;
            return;
        }
    }
}

bool work_page_mask_has_valid_home(uint8_t page_mask)
{
    return page_mask_has_non_xiaozhi_page(page_mask);
}

bool work_page_order_has_valid_home()
{
    int first_enabled = first_enabled_work_page_order_index_or_invalid_normalized();
    return work_page_order_index_found(first_enabled) &&
           g_work_page_order[first_enabled] != kWorkPageXiaozhiAI;
}

int next_enabled_work_page(int current_page)
{
    if (!is_work_page_index(current_page)) {
        current_page = first_enabled_work_page();
    }
    normalize_work_page_order();
    int current_index = work_page_order_index_normalized(current_page);
    int next_index = next_enabled_work_page_order_index_or_invalid_normalized(current_index);
    return work_page_order_index_found(next_index) ? g_work_page_order[next_index] : kFallbackWorkPage;
}

int first_enabled_work_page_order_index()
{
    normalize_work_page_order();
    return first_enabled_work_page_order_index_normalized();
}

int next_enabled_work_page_order_index(int current_order_index)
{
    normalize_work_page_order();
    current_order_index = valid_enabled_work_page_order_index_normalized(current_order_index);
    int next_index = next_enabled_work_page_order_index_or_invalid_normalized(current_order_index);
    return work_page_order_index_found(next_index) ? next_index : first_enabled_work_page_order_index_normalized();
}

int valid_enabled_work_page_order_index(int current_order_index)
{
    normalize_work_page_order();
    return valid_enabled_work_page_order_index_normalized(current_order_index);
}

void ensure_active_work_page_enabled()
{
    if (!is_work_page_enabled(active_work_page_load())) {
        active_work_page_store(first_enabled_work_page());
    }
}

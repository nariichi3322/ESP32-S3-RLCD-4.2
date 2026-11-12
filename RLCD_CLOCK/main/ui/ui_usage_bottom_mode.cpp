#include "ui_usage_bottom_mode.h"

#include <atomic>

namespace {
struct BottomMode {
    std::atomic<UsageBottomSection> section{UsageBottomSection::Grants};
    std::atomic<uint32_t> version{0};
};
BottomMode s_claude;
BottomMode s_codex;

BottomMode &mode(UsagePage page)
{
    return page == UsagePage::Claude ? s_claude : s_codex;
}
}

UsageBottomSection usage_bottom_section_load(UsagePage page)
{
    return mode(page).section.load(std::memory_order_acquire);
}

uint32_t usage_bottom_section_version(UsagePage page)
{
    return mode(page).version.load(std::memory_order_acquire);
}

void usage_bottom_section_toggle(UsagePage page)
{
    BottomMode &current = mode(page);
    const UsageBottomSection previous = current.section.load(std::memory_order_acquire);
    current.section.store(previous == UsageBottomSection::Grants
                              ? UsageBottomSection::Stats : UsageBottomSection::Grants,
                          std::memory_order_release);
    current.version.fetch_add(1, std::memory_order_release);
}

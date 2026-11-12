#include "ui_usage_bottom_mode.h"
#include "ui_usage_rows.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main()
{
    assert(usage_bottom_section_load(UsagePage::Claude) == UsageBottomSection::Grants);
    assert(usage_bottom_section_load(UsagePage::Codex) == UsageBottomSection::Grants);
    assert(usage_bottom_section_version(UsagePage::Claude) == 0);
    usage_bottom_section_toggle(UsagePage::Claude);
    assert(usage_bottom_section_load(UsagePage::Claude) == UsageBottomSection::Stats);
    assert(usage_bottom_section_load(UsagePage::Codex) == UsageBottomSection::Grants);
    assert(usage_bottom_section_version(UsagePage::Claude) == 1);
    assert(usage_bottom_section_version(UsagePage::Codex) == 0);
    usage_bottom_section_toggle(UsagePage::Codex);
    usage_bottom_section_toggle(UsagePage::Claude);
    assert(usage_bottom_section_load(UsagePage::Claude) == UsageBottomSection::Grants);
    assert(usage_bottom_section_load(UsagePage::Codex) == UsageBottomSection::Stats);
    assert(usage_bottom_section_version(UsagePage::Claude) == 2);
    usage_bottom_section_toggle(UsagePage::Codex);
    assert(usage_bottom_section_load(UsagePage::Codex) == UsageBottomSection::Grants);
    assert(usage_bottom_section_version(UsagePage::Codex) == 2);

    assert(usage_spent_percent(786, 4000) == 19);
    assert(usage_spent_percent(10001, 10000) == 100);
    assert(usage_spent_percent(UINT64_MAX, UINT64_MAX) == 100);
    assert(usage_spent_percent(UINT64_MAX - 1, UINT64_MAX) == 99);
    assert(usage_spent_percent(UINT64_MAX, UINT64_MAX / 2 + 1) == 199);
    assert(usage_spent_percent(UINT64_MAX, 1) == 999);
    char text[64];
    usage_format_amount(786, 2, text, sizeof(text));
    assert(strcmp(text, "7.86") == 0);
    usage_format_amount(123456, 2, text, sizeof(text));
    assert(strcmp(text, "1235") == 0);
    usage_format_amount(123449, 2, text, sizeof(text));
    assert(strcmp(text, "1234") == 0);
    usage_format_amount(999999999999ULL, 9, text, sizeof(text));
    assert(strcmp(text, "999.999999999") == 0);
    usage_format_amount(1000500000000ULL, 9, text, sizeof(text));
    assert(strcmp(text, "1001") == 0);
    usage_format_amount(UINT64_MAX, 0, text, sizeof(text));
    assert(strcmp(text, "18446744073709551615") == 0);
}

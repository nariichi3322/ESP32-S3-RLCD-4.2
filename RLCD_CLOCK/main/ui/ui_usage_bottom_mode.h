#pragma once

#include <stdint.h>

enum class UsageBottomSection : uint8_t { Grants, Stats };
enum class UsagePage : uint8_t { Claude, Codex };

UsageBottomSection usage_bottom_section_load(UsagePage page);
uint32_t usage_bottom_section_version(UsagePage page);
void usage_bottom_section_toggle(UsagePage page);

#pragma once

#include <stdint.h>

using TickType_t = uint32_t;

inline constexpr int pdTRUE = 1;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;


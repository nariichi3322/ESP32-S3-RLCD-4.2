#pragma once

#include "work_page_ids.h"

constexpr bool codex_ble_page_should_run(int active_page,
                                         bool low_battery,
                                         bool setup_portal,
                                         bool auxiliary_page,
                                         bool settings_page,
                                         bool ota_updating)
{
    return active_page == kWorkPageCodexUsage && !low_battery &&
           !setup_portal && !ota_updating &&
           (!auxiliary_page || settings_page);
}

constexpr bool codex_ble_icon_should_show(int page,
                                          bool normal_work_surface,
                                          bool codex_enabled)
{
    return normal_work_surface && page == kWorkPageCodexUsage && codex_enabled;
}

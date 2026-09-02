#pragma once

#include "ui_settings_contract.h"

constexpr int system_settings_page_for_selection(int selection)
{
    return selection >= 0 && selection < kSystemSettingsSecondaryCount
               ? selection / kSystemSettingsPageItemCount
               : 0;
}

constexpr bool system_settings_item_on_page(int item, int page)
{
    return item >= 0 && item < kSystemSettingsSecondaryCount && page >= 0 &&
           page < kSystemSettingsPageCount &&
           item / kSystemSettingsPageItemCount == page;
}

constexpr int system_settings_page_slot(int item)
{
    return item >= 0 && item < kSystemSettingsSecondaryCount
               ? item % kSystemSettingsPageItemCount
               : -1;
}

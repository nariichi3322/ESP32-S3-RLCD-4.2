#include "ui_settings_pagination.h"

#include <assert.h>

int main()
{
    static_assert(kSystemSettingsPageCount == 3);
    static_assert(kSystemSettingsPageItemCount == 4);
    assert(system_settings_page_for_selection(kSystemSettingsOfflineItem) == 0);
    assert(system_settings_page_for_selection(kSystemSettingsCalendarDisplayItem) == 0);
    assert(system_settings_page_slot(kSystemSettingsCalendarDisplayItem) ==
           system_settings_page_slot(kSystemSettingsLanguageItem) + 1);
    assert(system_settings_page_for_selection(kSystemSettingsInfoItem) == 1);
    assert(system_settings_page_for_selection(kSystemSettingsOtaItem) == 1);
    assert(system_settings_page_for_selection(kSystemSettingsFactoryResetItem) == 2);
    for (int item = 0; item < kSystemSettingsSecondaryCount; ++item) {
        const int page = system_settings_page_for_selection(item);
        assert(system_settings_item_on_page(item, page));
        assert(system_settings_page_slot(item) == item % 4);
        for (int other_page = 0; other_page < kSystemSettingsPageCount; ++other_page) {
            if (other_page != page) {
                assert(!system_settings_item_on_page(item, other_page));
            }
        }
    }
    assert(system_settings_page_for_selection(-1) == 0);
    assert(system_settings_page_slot(kSystemSettingsSecondaryCount) == -1);
    return 0;
}

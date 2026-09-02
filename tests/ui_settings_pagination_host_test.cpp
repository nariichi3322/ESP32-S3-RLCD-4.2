#include "ui_settings_pagination.h"

#include <assert.h>

int main()
{
    static_assert(kSystemSettingsPageCount == 2);
    static_assert(kSystemSettingsPageItemCount == 4);
    assert(system_settings_page_for_selection(kSystemSettingsOfflineItem) == 0);
    assert(system_settings_page_for_selection(kSystemSettingsInfoItem) == 0);
    assert(system_settings_page_for_selection(kSystemSettingsOtaItem) == 1);
    assert(system_settings_page_for_selection(kSystemSettingsFactoryResetItem) == 1);
    for (int item = 0; item < kSystemSettingsSecondaryCount; ++item) {
        const int page = system_settings_page_for_selection(item);
        assert(system_settings_item_on_page(item, page));
        assert(!system_settings_item_on_page(item, 1 - page));
        assert(system_settings_page_slot(item) == item % 4);
    }
    assert(system_settings_page_for_selection(-1) == 0);
    assert(system_settings_page_slot(kSystemSettingsSecondaryCount) == -1);
    return 0;
}

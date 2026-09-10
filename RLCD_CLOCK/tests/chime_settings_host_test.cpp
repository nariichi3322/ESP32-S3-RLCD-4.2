// 验证提醒音量默认值、NVS 异常值归一化和设置页循环档位规则。
#include "chime_settings.h"

#include <assert.h>

namespace {
uint8_t legacy_normalize_stored_volume(uint8_t volume)
{
    return volume <= 100 ? volume : 80;
}

} // namespace

int main()
{
    static_assert(chime_settings::kVolumeLevelCount == 6,
                  "settings UI exposes mute plus five audible levels");
    static_assert(chime_settings::kDefaultVolumePercent == 80,
                  "existing startup volume must remain 80 percent");
    static_assert(chime_settings::kSoundCount == 4,
                  "existing settings UI exposes four sounds");

    assert(chime_settings::normalize_stored_volume(0) == 0);
    assert(chime_settings::normalize_stored_volume(1) == 1);
    assert(chime_settings::normalize_stored_volume(80) == 80);
    assert(chime_settings::normalize_stored_volume(100) == 100);
    assert(chime_settings::normalize_stored_volume(101) == 80);
    assert(chime_settings::normalize_stored_volume(255) == 80);

    assert(chime_settings::next_volume_percent(-1) == 0);
    assert(chime_settings::next_volume_percent(0) == 20);
    assert(chime_settings::next_volume_percent(19) == 20);
    assert(chime_settings::next_volume_percent(20) == 40);
    assert(chime_settings::next_volume_percent(21) == 40);
    assert(chime_settings::next_volume_percent(40) == 60);
    assert(chime_settings::next_volume_percent(60) == 80);
    assert(chime_settings::next_volume_percent(80) == 100);
    assert(chime_settings::next_volume_percent(100) == 0);
    assert(chime_settings::next_volume_percent(101) == 0);

    for (int value = 0; value <= 255; ++value) {
        uint8_t stored = static_cast<uint8_t>(value);
        assert(chime_settings::normalize_stored_volume(stored) ==
               legacy_normalize_stored_volume(stored));
    }
    return 0;
}

// 私有保存设置页二次确认状态，避免确认标志泄漏到全局应用状态。
#include "ui_settings_confirmation_state.h"

namespace {
bool s_factory_reset_pending;
bool s_offline_disable_pending;
bool s_weather_city_clear_pending;

bool *confirmation_slot(SettingsConfirmation confirmation)
{
    switch (confirmation) {
    case SettingsConfirmation::kFactoryReset:
        return &s_factory_reset_pending;
    case SettingsConfirmation::kOfflineDisable:
        return &s_offline_disable_pending;
    case SettingsConfirmation::kWeatherCityClear:
        return &s_weather_city_clear_pending;
    }
    return nullptr;
}
} // namespace

bool settings_confirmation_pending(SettingsConfirmation confirmation)
{
    const bool *pending = confirmation_slot(confirmation);
    return pending && *pending;
}

void settings_confirmation_request(SettingsConfirmation confirmation)
{
    bool *pending = confirmation_slot(confirmation);
    if (pending) {
        *pending = true;
    }
}

void settings_confirmation_clear(SettingsConfirmation confirmation)
{
    bool *pending = confirmation_slot(confirmation);
    if (pending) {
        *pending = false;
    }
}

void settings_confirmation_clear_all()
{
    s_factory_reset_pending = false;
    s_offline_disable_pending = false;
    s_weather_city_clear_pending = false;
}

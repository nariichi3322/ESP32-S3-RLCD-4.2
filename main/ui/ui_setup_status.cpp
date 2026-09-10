// 构建并刷新配网模式下显示的 AP、门户和 STA 状态行。
#include "ui_setup_status.h"

#include "app_constexpr.h"
#include "app_metadata.h"
#include "app_network_config.h"
#include "network_credentials_state.h"
#include "ui_language.h"
#include "ui_page_state.h"
#include "ui_i18n.h"
#include "ui_text_format.h"
#include "ui_widgets.h"
#include "wifi_portal_state.h"

#include "esp_attr.h"
#include "esp_log.h"

namespace {
#define SETUP_STATUS_LABEL_CREATE_FAILED_FORMAT "setup status label create failed index=%d"
#define SETUP_STATUS_LINE_INDEX_OUT_OF_RANGE_FORMAT "setup status line index out of range: %u"

constexpr int kSetupStatusLabelX = 26;
constexpr int kSetupStatusLabelWidth = 348;
constexpr int kSetupStatusLabelHeight = 18;
constexpr int kSetupStatusLabelY[] = {194, 212, 230, 248, 266, 284};
EXT_RAM_BSS_ATTR lv_obj_t *s_setup_status_labels[array_count(kSetupStatusLabelY)];
struct SetupStatusText {
    UiTextId id;
};

const char *setup_status_text(const SetupStatusText &text)
{
    return ui_text(text.id);
}

const char *setup_status_placeholder()
{
    return ui_text(UiTextId::UiPlaceholder);
}

template <size_t Count>
constexpr bool setup_status_texts_nonempty(const SetupStatusText (&items)[Count])
{
    for (const SetupStatusText &item : items) {
        if (static_cast<unsigned>(item.id) >= static_cast<unsigned>(UiTextId::Count)) {
            return false;
        }
    }
    return true;
}

constexpr SetupStatusText kSetupStatusInitialText[] = {
    {UiTextId::SetupModeTitle},
    {UiTextId::SetupApSsidPlaceholder},
    {UiTextId::SetupApPasswordPlaceholder},
    {UiTextId::SetupPortalIpPlaceholder},
    {UiTextId::SetupStaSsidPlaceholder},
    {UiTextId::SetupStaIpPlaceholder},
};
constexpr size_t kSetupStatusLineSize = 96;
constexpr SetupStatusText kSetupStatusTitle = {UiTextId::SetupModeTitle};
constexpr SetupStatusText kSetupApSsidFormat = {UiTextId::SetupApSsidFormat};
constexpr SetupStatusText kSetupApPasswordFormat = {UiTextId::SetupApPasswordFormat};
constexpr SetupStatusText kSetupPortalIpFormat = {UiTextId::SetupPortalIpFormat};
constexpr SetupStatusText kSetupStaSsidFormat = {UiTextId::SetupStaSsidFormat};
constexpr SetupStatusText kSetupStaIpFormat = {UiTextId::SetupStaIpFormat};
constexpr SetupStatusText kSetupStaIpReasonFormat = {UiTextId::SetupStaIpReasonFormat};
constexpr SetupStatusText kSetupStaIpPlaceholder = {UiTextId::SetupStaIpPlaceholder};
constexpr size_t kSetupStatusTitleIndex = 0;
constexpr size_t kSetupStatusApSsidIndex = 1;
constexpr size_t kSetupStatusApPasswordIndex = 2;
constexpr size_t kSetupStatusPortalIpIndex = 3;
constexpr size_t kSetupStatusStaSsidIndex = 4;
constexpr size_t kSetupStatusStaIpIndex = 5;
template <typename... Args>
bool set_setup_status_line(size_t index, const char *fallback, const char *format, Args... args)
{
    if (index >= array_count(s_setup_status_labels)) {
        ESP_LOGW(TAG, SETUP_STATUS_LINE_INDEX_OUT_OF_RANGE_FORMAT, (unsigned)index);
        return false;
    }
    char line[kSetupStatusLineSize] = {};
    ui_text_format::format_or_fallback(line, sizeof(line), fallback, format, args...);
    return set_label_text_if_changed(s_setup_status_labels[index], line);
}

static_assert(array_count(kSetupStatusLabelY) == array_count(kSetupStatusInitialText),
              "setup status coordinates and text must stay in sync");
static_assert(array_count(kSetupStatusLabelY) == array_count(s_setup_status_labels),
              "setup status label storage must match the rendered row count");
static_assert(kSetupStatusStaIpIndex < array_count(s_setup_status_labels),
              "setup status semantic indices must fit label storage");
static_assert(setup_status_texts_nonempty(kSetupStatusInitialText),
              "setup status initial texts must be non-empty");
} // namespace

void build_setup_status_panel(lv_obj_t *parent)
{
    for (size_t i = 0; i < array_count(kSetupStatusLabelY); ++i) {
        s_setup_status_labels[i] = make_label_with_font(parent,
                                                        kSetupStatusLabelX,
                                                        kSetupStatusLabelY[i],
                                                        kSetupStatusLabelWidth,
                                                        kSetupStatusLabelHeight,
                                                        setup_status_text(kSetupStatusInitialText[i]),
                                                        &lv_font_montserrat_14);
        if (s_setup_status_labels[i]) {
            lv_obj_add_flag(s_setup_status_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW(TAG, SETUP_STATUS_LABEL_CREATE_FAILED_FORMAT, static_cast<int>(i));
        }
    }
}

bool update_setup_status_panel()
{
    char wifi_ssid[kNetworkWifiSsidLen] = {};
    char setup_ap_ssid[kWifiSetupApSsidTextLen] = {};
    (void)network_wifi_ssid_snapshot(wifi_ssid, sizeof(wifi_ssid));
    (void)wifi_setup_ap_ssid_snapshot(setup_ap_ssid, sizeof(setup_ap_ssid));
    bool changed = false;
    if (!s_setup_status_labels[kSetupStatusTitleIndex]) {
        return false;
    }
    changed |= set_label_text_if_changed(s_setup_status_labels[kSetupStatusTitleIndex],
                                         setup_status_text(kSetupStatusTitle));
    changed |= set_setup_status_line(kSetupStatusApSsidIndex,
                                     setup_status_placeholder(),
                                     setup_status_text(kSetupApSsidFormat),
                                     setup_ap_ssid[0] ? setup_ap_ssid : setup_status_placeholder());
    changed |= set_setup_status_line(kSetupStatusApPasswordIndex,
                                     setup_status_placeholder(),
                                     setup_status_text(kSetupApPasswordFormat),
                                     kSetupApPassword);
    changed |= set_setup_status_line(kSetupStatusPortalIpIndex,
                                     setup_status_placeholder(),
                                     setup_status_text(kSetupPortalIpFormat),
                                     kSetupPortalIp);
    changed |= set_setup_status_line(kSetupStatusStaSsidIndex,
                                     setup_status_placeholder(),
                                     setup_status_text(kSetupStaSsidFormat),
                                     wifi_ssid[0] ? wifi_ssid : setup_status_placeholder());
    char station_ip[kWifiStationIpTextLen] = {};
    const bool have_station_ip = wifi_station_ip_snapshot(station_ip, sizeof(station_ip));
    const int disconnect_reason = wifi_last_disconnect_reason();
    if (have_station_ip) {
        changed |= set_setup_status_line(
            kSetupStatusStaIpIndex,
            setup_status_text(kSetupStaIpPlaceholder),
            setup_status_text(kSetupStaIpFormat),
            station_ip);
    } else if (disconnect_reason) {
        changed |= set_setup_status_line(kSetupStatusStaIpIndex,
                                         setup_status_text(kSetupStaIpPlaceholder),
                                         setup_status_text(kSetupStaIpReasonFormat),
                                         disconnect_reason);
    } else {
        char line[kSetupStatusLineSize] = {};
        ui_text_format::copy(line, sizeof(line), setup_status_text(kSetupStaIpPlaceholder));
        changed |= set_label_text_if_changed(s_setup_status_labels[kSetupStatusStaIpIndex], line);
    }
    return changed;
}

void set_setup_status_panel_visible(bool visible)
{
    for (lv_obj_t *label : s_setup_status_labels) {
        set_obj_visible(label, visible);
    }
}

void clear_setup_status_object_refs()
{
    for (lv_obj_t *&label : s_setup_status_labels) {
        label = nullptr;
    }
}

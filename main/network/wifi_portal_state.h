// 声明 Wi-Fi 门户、断线原因、AP 名称和本地 IP 的跨任务安全访问接口。
#pragma once

#include <stddef.h>
#include <stdint.h>

inline constexpr size_t kWifiStationIpTextLen = 16;
inline constexpr size_t kWifiSetupApSsidTextLen = 33;

enum class WifiPortalSaveResult : uint8_t {
    kNone = 0,
    kValidating,
    kInvalidInput,
    kWifiConnectionFailed,
    kWeatherApiFailed,
    kWeatherCityInvalid,
    kSuccess,
};

inline constexpr bool wifi_portal_result_preserves_client_lease(
    WifiPortalSaveResult result)
{
    return result == WifiPortalSaveResult::kValidating ||
           result == WifiPortalSaveResult::kSuccess;
}

bool wifi_portal_state_init();
bool setup_portal_active_load();
void setup_portal_active_store(bool active);
int wifi_last_disconnect_reason();
void record_wifi_disconnect_reason(int reason);
void clear_wifi_last_disconnect_reason();
bool wifi_setup_ap_ssid_snapshot(char *out, size_t out_len);
void wifi_setup_ap_ssid_store(const char *ssid);
bool wifi_station_ip_snapshot(char *out, size_t out_len);
void wifi_station_ip_store(const char *ip_text);
void clear_wifi_station_ip();
WifiPortalSaveResult wifi_portal_save_result_load();
void wifi_portal_save_result_store(WifiPortalSaveResult result);
bool wifi_portal_save_feedback_seen_load();
void wifi_portal_save_feedback_seen_store(bool seen);

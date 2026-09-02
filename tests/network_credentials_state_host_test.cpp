// 驗證 Open-Meteo 模式只以 Wi-Fi 憑據判斷線上服務可用性。
#include "network_credentials_state.h"
#include "network_credentials_state_internal.h"

#include <assert.h>
#include <atomic>
#include <string.h>

std::atomic<bool> g_fail_mutex_take{false};
std::atomic<int> g_mutex_take_count{0};
std::atomic<int> g_mutex_give_count{0};
std::atomic<int> g_mutex_active_holds{0};

int main()
{
    assert(network_credentials_state_init());
    network_credentials_clear();
    assert(!network_wifi_credentials_configured());
    assert(!network_weather_configuration_configured());

    network_credentials_store("primary", "secret", "backup", "backup-secret",
                              WifiCredentialSlot::kSlotA);
    assert(network_wifi_credentials_configured());
    assert(network_weather_configuration_configured());
    assert(network_all_online_credentials_configured());
    assert(network_credentials_availability().wifi_configured);

    char ssid[kNetworkWifiSsidLen] = {};
    char password[kNetworkWifiPasswordLen] = {};
    assert(network_wifi_credentials_copy(ssid, sizeof(ssid), password, sizeof(password)));
    assert(strcmp(ssid, "primary") == 0);
    assert(strcmp(password, "secret") == 0);

    assert(network_wifi_select_slot(WifiCredentialSlot::kSlotB));
    assert(network_wifi_credentials_copy(ssid, sizeof(ssid), password, sizeof(password)));
    assert(strcmp(ssid, "backup") == 0);
    assert(strcmp(password, "backup-secret") == 0);

    network_credentials_clear();
    assert(!network_all_online_credentials_configured());
    return 0;
}

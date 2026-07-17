// 集中维护配网页活跃状态、断线原因、AP 名称和本地 IP 完整快照。
#include "wifi_portal_state.h"

#include "scoped_semaphore_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <string.h>

namespace {
std::atomic<bool> s_setup_portal_active{false};
std::atomic<int> s_last_wifi_disconnect_reason{0};
StaticSemaphore_t s_portal_text_mutex_storage = {};
SemaphoreHandle_t s_portal_text_mutex = nullptr;
char s_setup_ap_ssid[kWifiSetupApSsidTextLen] = {};
char s_station_ip[kWifiStationIpTextLen] = {};

template <size_t N>
bool portal_text_snapshot(const char (&source)[N], char *out, size_t out_len)
{
    if (!out || out_len < N) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }
    ScopedSemaphoreLock lock(s_portal_text_mutex);
    if (!lock) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, source, N);
    return source[0] != '\0';
}

template <size_t N>
void portal_text_store(char (&target)[N], const char *value)
{
    char replacement[N] = {};
    strlcpy(replacement, value ? value : "", sizeof(replacement));
    ScopedSemaphoreLock lock(s_portal_text_mutex);
    if (!lock) {
        return;
    }
    memcpy(target, replacement, N);
}
} // namespace

bool wifi_portal_state_init()
{
    if (s_portal_text_mutex) {
        return true;
    }
    s_portal_text_mutex = xSemaphoreCreateMutexStatic(&s_portal_text_mutex_storage);
    return s_portal_text_mutex != nullptr;
}

bool setup_portal_active_load()
{
    return s_setup_portal_active.load(std::memory_order_acquire);
}

void setup_portal_active_store(bool active)
{
    s_setup_portal_active.store(active, std::memory_order_release);
}

int wifi_last_disconnect_reason()
{
    return s_last_wifi_disconnect_reason.load(std::memory_order_acquire);
}

void record_wifi_disconnect_reason(int reason)
{
    s_last_wifi_disconnect_reason.store(reason, std::memory_order_release);
}

void clear_wifi_last_disconnect_reason()
{
    record_wifi_disconnect_reason(0);
}

bool wifi_setup_ap_ssid_snapshot(char *out, size_t out_len)
{
    return portal_text_snapshot(s_setup_ap_ssid, out, out_len);
}

void wifi_setup_ap_ssid_store(const char *ssid)
{
    portal_text_store(s_setup_ap_ssid, ssid);
}

bool wifi_station_ip_snapshot(char *out, size_t out_len)
{
    return portal_text_snapshot(s_station_ip, out, out_len);
}

void wifi_station_ip_store(const char *ip_text)
{
    portal_text_store(s_station_ip, ip_text);
}

void clear_wifi_station_ip()
{
    wifi_station_ip_store("");
}

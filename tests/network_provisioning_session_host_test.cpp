// 验证配网热点三态启动、结果反馈等待和失败门户保留事务。
#include "network_provisioning_session.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "network_task_guards.h"

#include <cassert>

const char *const TAG = "ProvisioningSessionHost";

namespace {

bool s_start_requested = false;
bool s_start_wifi_result = false;
int s_start_wifi_calls = 0;
EventBits_t s_cleared_bits = 0;
int s_settings_clear_calls = 0;
int s_diag_clear_calls = 0;
int s_info_clear_calls = 0;
int s_notify_calls = 0;

bool s_portal_active = false;
bool s_feedback_seen = false;
TickType_t s_ticks = 0;
int s_delay_calls = 0;
TickType_t s_last_delay = 0;

bool s_prepare_result = false;
int s_prepare_calls = 0;
WifiPortalSaveResult s_stored_result = WifiPortalSaveResult::kNone;
int s_store_calls = 0;
bool s_awake_lock_available = true;
int s_awake_acquire_calls = 0;
int s_awake_release_calls = 0;

void reset_start_state()
{
    s_start_requested = false;
    s_start_wifi_result = false;
    s_start_wifi_calls = 0;
    s_cleared_bits = 0;
    s_settings_clear_calls = 0;
    s_diag_clear_calls = 0;
    s_info_clear_calls = 0;
    s_notify_calls = 0;
}

void reset_feedback_state()
{
    s_portal_active = false;
    s_feedback_seen = false;
    s_ticks = 0;
    s_delay_calls = 0;
    s_last_delay = 0;
}

} // namespace

bool setup_portal_start_requested()
{
    return s_start_requested;
}

bool start_wifi_radio(bool force_setup_portal)
{
    assert(force_setup_portal);
    ++s_start_wifi_calls;
    return s_start_wifi_result;
}

EventBits_t app_event_group_clear_bits(EventBits_t bits)
{
    s_cleared_bits |= bits;
    return s_cleared_bits;
}

void settings_page_clear()
{
    ++s_settings_clear_calls;
}

void network_diag_page_clear()
{
    ++s_diag_clear_calls;
}

void info_page_clear()
{
    ++s_info_clear_calls;
}

void notify_ui_task()
{
    ++s_notify_calls;
}

bool setup_portal_active_load()
{
    return s_portal_active;
}

bool wifi_portal_save_feedback_seen_load()
{
    return s_feedback_seen;
}

TickType_t xTaskGetTickCount()
{
    return s_ticks;
}

void vTaskDelay(TickType_t ticks)
{
    s_last_delay = ticks;
    s_ticks += ticks;
    ++s_delay_calls;
}

bool prepare_setup_portal_result_delivery()
{
    ++s_prepare_calls;
    return s_prepare_result;
}

void wifi_portal_save_result_store(WifiPortalSaveResult result)
{
    s_stored_result = result;
    ++s_store_calls;
}

bool acquire_network_awake_lock()
{
    ++s_awake_acquire_calls;
    return s_awake_lock_available;
}

void release_network_awake_lock()
{
    ++s_awake_release_calls;
}

int main()
{
    reset_start_state();
    assert(service_setup_portal_start_request() == SetupPortalStartResult::kNoRequest);
    assert(s_start_wifi_calls == 0);

    reset_start_state();
    s_start_requested = true;
    assert(service_setup_portal_start_request() == SetupPortalStartResult::kRetryPending);
    assert(s_start_wifi_calls == 1);
    assert(s_cleared_bits == 0);
    assert(s_notify_calls == 0);

    reset_start_state();
    s_start_requested = true;
    s_start_wifi_result = true;
    assert(service_setup_portal_start_request() == SetupPortalStartResult::kStarted);
    assert(s_start_wifi_calls == 1);
    assert(s_cleared_bits == kSetupPortalStartBit);
    assert(s_settings_clear_calls == 1);
    assert(s_diag_clear_calls == 1);
    assert(s_info_clear_calls == 1);
    assert(s_notify_calls == 1);

    reset_feedback_state();
    wait_for_provisioning_result_feedback();
    assert(s_delay_calls == 0);
    assert(s_ticks == 0);

    reset_feedback_state();
    s_portal_active = true;
    wait_for_provisioning_result_feedback();
    assert(s_delay_calls == 300);
    assert(s_ticks == pdMS_TO_TICKS(30000));
    assert(s_last_delay == pdMS_TO_TICKS(100));

    reset_feedback_state();
    s_portal_active = true;
    s_feedback_seen = true;
    wait_for_provisioning_result_feedback();
    assert(s_delay_calls == 1);
    assert(s_ticks == pdMS_TO_TICKS(750));
    assert(s_last_delay == pdMS_TO_TICKS(750));

    s_prepare_result = true;
    s_prepare_calls = 0;
    s_stored_result = WifiPortalSaveResult::kNone;
    s_store_calls = 0;
    s_awake_acquire_calls = 0;
    s_awake_release_calls = 0;
    {
        NetworkAwakeLockGuard awake_lock;
        assert(awake_lock.locked());
        keep_setup_portal_after_provisioning_failure(
            awake_lock,
            WifiPortalSaveResult::kWeatherApiFailed);
        assert(!awake_lock.locked());
    }
    assert(s_prepare_calls == 1);
    assert(s_store_calls == 1);
    assert(s_stored_result == WifiPortalSaveResult::kWeatherApiFailed);
    assert(s_awake_acquire_calls == 1);
    assert(s_awake_release_calls == 1);

    s_awake_lock_available = false;
    s_awake_acquire_calls = 0;
    s_awake_release_calls = 0;
    {
        NetworkAwakeLockGuard awake_lock;
        assert(!awake_lock.locked());
        keep_setup_portal_after_provisioning_failure(
            awake_lock,
            WifiPortalSaveResult::kWifiConnectionFailed);
    }
    assert(s_awake_acquire_calls == 1);
    assert(s_awake_release_calls == 0);

    return 0;
}

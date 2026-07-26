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
bool s_stop_requested = false;
bool s_stop_succeeds = false;
bool s_wifi_radio_on = false;
int s_stop_wifi_calls = 0;
int s_complete_stop_calls = 0;

bool s_portal_active = false;
bool s_feedback_seen = false;
TickType_t s_ticks = 0;
int s_delay_calls = 0;
TickType_t s_last_delay = 0;
int s_wait_calls = 0;
EventBits_t s_wait_bits = 0;
TickType_t s_wait_timeout = 0;
bool s_event_group_ready = true;

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

void reset_stop_state()
{
    s_stop_requested = false;
    s_stop_succeeds = false;
    s_wifi_radio_on = false;
    s_portal_active = false;
    s_stop_wifi_calls = 0;
    s_complete_stop_calls = 0;
}

void reset_feedback_state()
{
    s_portal_active = false;
    s_feedback_seen = false;
    s_ticks = 0;
    s_delay_calls = 0;
    s_last_delay = 0;
    s_wait_calls = 0;
    s_wait_bits = 0;
    s_wait_timeout = 0;
    s_event_group_ready = true;
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

bool setup_portal_stop_requested()
{
    return s_stop_requested;
}

void stop_wifi_radio(bool force_setup_portal)
{
    assert(force_setup_portal);
    ++s_stop_wifi_calls;
    if (s_stop_succeeds) {
        s_wifi_radio_on = false;
        s_portal_active = false;
    }
}

bool wifi_radio_on_load()
{
    return s_wifi_radio_on;
}

void complete_setup_portal_stop_request()
{
    s_stop_requested = false;
    ++s_complete_stop_calls;
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

bool app_event_group_ready()
{
    return s_event_group_ready;
}

EventBits_t app_event_group_wait_bits(EventBits_t bits,
                                      BaseType_t clear_on_exit,
                                      BaseType_t wait_for_all,
                                      TickType_t timeout)
{
    assert(clear_on_exit == pdTRUE);
    assert(wait_for_all == pdFALSE);
    ++s_wait_calls;
    s_wait_bits = bits;
    s_wait_timeout = timeout;
    s_ticks += timeout;
    return 0;
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

    reset_stop_state();
    assert(service_setup_portal_stop_request() == SetupPortalStopResult::kNoRequest);
    assert(s_stop_wifi_calls == 0);
    assert(s_complete_stop_calls == 0);

    reset_stop_state();
    s_stop_requested = true;
    s_wifi_radio_on = true;
    s_portal_active = true;
    assert(service_setup_portal_stop_request() == SetupPortalStopResult::kRetryPending);
    assert(s_stop_wifi_calls == 1);
    assert(s_stop_requested);
    assert(s_complete_stop_calls == 0);

    reset_stop_state();
    s_stop_requested = true;
    s_stop_succeeds = true;
    s_wifi_radio_on = true;
    s_portal_active = true;
    assert(service_setup_portal_stop_request() == SetupPortalStopResult::kStopped);
    assert(s_stop_wifi_calls == 1);
    assert(!s_stop_requested);
    assert(s_complete_stop_calls == 1);

    reset_feedback_state();
    wait_for_provisioning_result_feedback();
    assert(s_delay_calls == 0);
    assert(s_ticks == 0);

    reset_feedback_state();
    s_portal_active = true;
    wait_for_provisioning_result_feedback();
    assert(s_wait_calls == 1);
    assert(s_wait_bits == kProvisioningFeedbackBit);
    assert(s_wait_timeout == pdMS_TO_TICKS(30000));
    assert(s_delay_calls == 0);
    assert(s_ticks == pdMS_TO_TICKS(30000));

    reset_feedback_state();
    s_portal_active = true;
    s_feedback_seen = true;
    wait_for_provisioning_result_feedback();
    assert(s_wait_calls == 0);
    assert(s_delay_calls == 1);
    assert(s_ticks == pdMS_TO_TICKS(750));
    assert(s_last_delay == pdMS_TO_TICKS(750));

    reset_feedback_state();
    s_portal_active = true;
    s_event_group_ready = false;
    wait_for_provisioning_result_feedback();
    assert(s_wait_calls == 0);
    assert(s_delay_calls == 1);
    assert(s_ticks == pdMS_TO_TICKS(30000));
    assert(s_last_delay == pdMS_TO_TICKS(30000));

    reset_feedback_state();
    s_prepare_result = false;
    s_prepare_calls = 0;
    s_stored_result = WifiPortalSaveResult::kNone;
    s_store_calls = 0;
    publish_setup_portal_result(WifiPortalSaveResult::kWeatherApiFailed);
    assert(s_prepare_calls == 1);
    assert(s_delay_calls == 0);
    assert(s_store_calls == 1);
    assert(s_stored_result == WifiPortalSaveResult::kWeatherApiFailed);

    reset_feedback_state();
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
    assert(s_delay_calls == 0);
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

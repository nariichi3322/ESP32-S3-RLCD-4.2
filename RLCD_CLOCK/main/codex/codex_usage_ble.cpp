// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#include "codex_usage_ble.h"
#include "codex_ble_lifecycle_policy.h"

#include "codex_usage_state.h"
#include "ui_task_notify.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <atomic>
#include <stddef.h>

extern "C" void ble_store_config_init(void);

namespace {
constexpr const char *kTag = "codex_ble";
constexpr const char *kDeviceName = "Codex Display";
constexpr size_t kDeviceNameLength = sizeof("Codex Display") - 1U;
constexpr uint32_t kPairingOverlayMs = 75000;
constexpr uint32_t kLogThrottleMs = 5000;
constexpr uint32_t kFirstStatusTimeoutMs = 20000;
constexpr uint32_t kAdvertisingRetryDelaysMs[] = {1000, 5000, 15000, 30000};
constexpr uint32_t kHostStopRetryMs = 1000;
constexpr uint32_t kClearBondsTimeoutMs = 10000;
constexpr uint32_t kControlTaskStackWords = 4096;
constexpr UBaseType_t kControlTaskPriority = 3;
constexpr uint32_t kControlNotifyDesired = 1U << 0;
constexpr uint32_t kControlNotifyHostExited = 1U << 1;
constexpr uint32_t kControlNotifyClearBonds = 1U << 2;
constexpr uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;

static_assert(3U + 2U + 16U <= 31U,
              "flags and 128-bit service UUID must fit legacy advertising");
static_assert(2U + kDeviceNameLength <= 31U,
              "device name must fit legacy scan response");

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x01, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0,
    0x44, 0x4b, 0x6d, 0x8f, 0x20, 0x6c, 0x8b, 0x7d);
const ble_uuid128_t kStatusUuid = BLE_UUID128_INIT(
    0x02, 0xde, 0xc0, 0x70, 0x65, 0x1b, 0xf8, 0xa0,
    0x44, 0x4b, 0x6d, 0x8f, 0x20, 0x6c, 0x8b, 0x7d);

std::atomic<bool> s_initialized{false};
std::atomic<bool> s_running{false};
std::atomic<bool> s_stopping{false};
std::atomic<bool> s_host_exited{true};
std::atomic<uint16_t> s_connection{kNoConnection};
std::atomic<bool> s_connection_secure{false};
std::atomic<bool> s_status_received{false};
std::atomic<uint32_t> s_secure_since_tick{0};
esp_timer_handle_t s_first_status_timer = nullptr;
esp_timer_handle_t s_advertising_retry_timer = nullptr;
std::atomic<uint32_t> s_advertising_retry_attempt{0};
std::atomic<uint8_t> s_own_addr_type{0};
std::atomic<uint32_t> s_pairing_passkey{0};
std::atomic<uint32_t> s_pairing_expiry{0};
std::atomic<uint32_t> s_pairing_generation{0};
std::atomic<bool> s_pairing_visible{false};
std::atomic<uint32_t> s_last_reject_log_tick{0};
std::atomic<bool> s_desired_enabled{false};
std::atomic<TaskHandle_t> s_control_task{nullptr};
std::atomic_flag s_control_task_starting = ATOMIC_FLAG_INIT;
SemaphoreHandle_t s_clear_bonds_done = nullptr;
std::atomic<bool> s_clear_bonds_pending{false};
std::atomic<bool> s_clear_bonds_result{false};
uint16_t s_status_value_handle = 0;

uint32_t monotonic_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void publish_pairing(uint32_t passkey)
{
    s_pairing_passkey.store(passkey, std::memory_order_relaxed);
    s_pairing_expiry.store(monotonic_ms() + kPairingOverlayMs,
                           std::memory_order_relaxed);
    s_pairing_visible.store(true, std::memory_order_release);
    s_pairing_generation.fetch_add(1, std::memory_order_acq_rel);
    notify_ui_task();
}

void clear_pairing()
{
    if (s_pairing_visible.exchange(false, std::memory_order_acq_rel)) {
        s_pairing_generation.fetch_add(1, std::memory_order_acq_rel);
        notify_ui_task();
    }
}

void log_rejected_status(CodexUsageParseResult result)
{
    const uint32_t now = monotonic_ms();
    const uint32_t previous = s_last_reject_log_tick.load(std::memory_order_relaxed);
    if (static_cast<uint32_t>(now - previous) < kLogThrottleMs) return;
    s_last_reject_log_tick.store(now, std::memory_order_relaxed);
    ESP_LOGW(kTag, "rejected status payload: %u", static_cast<unsigned>(result));
}

int status_access(uint16_t conn_handle, uint16_t,
                  ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
        !desc.sec_state.encrypted || !desc.sec_state.authenticated ||
        !desc.sec_state.bonded) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length == 0 || length > kCodexUsageMaxPayloadBytes) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t payload[kCodexUsageMaxPayloadBytes];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, length, &copied) != 0 ||
        copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const CodexUsageParseResult result = codex_usage_state_submit(
        reinterpret_cast<const char *>(payload), copied, monotonic_ms());
    if (result != CodexUsageParseResult::Ok) {
        log_rejected_status(result);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    s_status_received.store(true, std::memory_order_release);
    if (s_first_status_timer) (void)esp_timer_stop(s_first_status_timer);
    // Even an unchanged heartbeat refreshes last_valid_tick_ms.  Wake the UI
    // task so it can cancel/re-arm its STALE deadline; the UI still avoids a
    // display redraw when the visible state and values did not change.
    notify_ui_task();
    return 0;
}

void first_status_watchdog(void *)
{
    const uint32_t now = monotonic_ms();
    const uint32_t secure_since =
        s_secure_since_tick.load(std::memory_order_acquire);
    if (s_running.load(std::memory_order_acquire) &&
        !s_stopping.load(std::memory_order_acquire) &&
        s_connection_secure.load(std::memory_order_acquire) &&
        !s_status_received.load(std::memory_order_acquire) &&
        static_cast<uint32_t>(now - secure_since) >= kFirstStatusTimeoutMs) {
        const uint16_t connection =
            s_connection.load(std::memory_order_acquire);
        if (connection != kNoConnection) {
            ESP_LOGW(kTag,
                     "no status after secure connection; disconnecting handle=%u",
                     connection);
            (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
}

void schedule_first_status_watchdog()
{
    if (!s_first_status_timer) return;
    (void)esp_timer_stop(s_first_status_timer);
    const esp_err_t result = esp_timer_start_once(
        s_first_status_timer,
        static_cast<uint64_t>(kFirstStatusTimeoutMs) * 1000ULL);
    if (result != ESP_OK) ESP_LOGW(kTag, "failed to arm first-status timer: %s",
                                   esp_err_to_name(result));
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kStatusUuid.u,
        .access_cb = status_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_WRITE_AUTHEN,
        .min_key_size = 16,
        .val_handle = &s_status_value_handle,
        .cpfd = nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .includes = nullptr,
        .characteristics = kCharacteristics,
    },
    {},
};

int start_advertising();
void start_advertising_with_retry();
void advertising_retry_timer_callback(void *);
void reset_transport_session_state();

int gap_event(ble_gap_event *event, void *)
{
    ble_gap_conn_desc desc{};
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(kTag, "connect status=%d handle=%u",
                 event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) {
            if (s_advertising_retry_timer) {
                (void)esp_timer_stop(s_advertising_retry_timer);
            }
            s_advertising_retry_attempt.store(0, std::memory_order_release);
            s_connection.store(event->connect.conn_handle,
                               std::memory_order_release);
            s_connection_secure.store(false, std::memory_order_release);
            s_status_received.store(false, std::memory_order_release);
            s_secure_since_tick.store(0, std::memory_order_release);
            codex_usage_state_connection_changed(true, false);
            notify_ui_task();
            return ble_gap_security_initiate(event->connect.conn_handle);
        }
        start_advertising_with_retry();
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(kTag, "disconnect reason=%d handle=%u",
                 event->disconnect.reason,
                 event->disconnect.conn.conn_handle);
        s_connection.store(kNoConnection, std::memory_order_release);
        s_connection_secure.store(false, std::memory_order_release);
        s_status_received.store(false, std::memory_order_release);
        s_secure_since_tick.store(0, std::memory_order_release);
        if (s_first_status_timer) (void)esp_timer_stop(s_first_status_timer);
        codex_usage_state_connection_changed(false, false);
        clear_pairing();
        notify_ui_task();
        start_advertising_with_retry();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising_with_retry();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            const bool secure = event->enc_change.status == 0 &&
                                desc.sec_state.encrypted &&
                                desc.sec_state.authenticated &&
                                desc.sec_state.bonded;
            ESP_LOGI(kTag,
                     "security status=%d encrypted=%u authenticated=%u bonded=%u",
                     event->enc_change.status, desc.sec_state.encrypted,
                     desc.sec_state.authenticated, desc.sec_state.bonded);
            s_connection_secure.store(secure, std::memory_order_release);
            codex_usage_state_connection_changed(true, secure);
            if (secure) {
                s_secure_since_tick.store(monotonic_ms(),
                                          std::memory_order_release);
                schedule_first_status_watchdog();
            } else if (s_first_status_timer) {
                (void)esp_timer_stop(s_first_status_timer);
            }
            if (secure || event->enc_change.status != 0) clear_pairing();
            notify_ui_task();
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(kTag, "passkey action=%u handle=%u",
                 event->passkey.params.action,
                 event->passkey.conn_handle);
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        } else {
            constexpr uint32_t kRange = 900000U;
            const uint32_t limit = UINT32_MAX - (UINT32_MAX % kRange);
            uint32_t random;
            do random = esp_random(); while (random >= limit);
            ble_sm_io io{};
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = 100000U + random % kRange;
            publish_pairing(io.passkey);
            return ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
    default:
        return 0;
    }
}

int start_advertising()
{
    if (!s_running.load(std::memory_order_acquire) ||
        s_stopping.load(std::memory_order_acquire)) return 0;
    if (s_connection.load(std::memory_order_acquire) != kNoConnection ||
        ble_gap_adv_active()) return 0;
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t *>(&kServiceUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    ble_hs_adv_fields response{};
    response.name = reinterpret_cast<uint8_t *>(
        const_cast<char *>(kDeviceName));
    response.name_len = kDeviceNameLength;
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_own_addr_type.load(std::memory_order_acquire),
                             nullptr, BLE_HS_FOREVER, &params, gap_event,
                             nullptr);
}

void schedule_advertising_retry()
{
    if (!s_advertising_retry_timer ||
        !s_running.load(std::memory_order_acquire) ||
        s_stopping.load(std::memory_order_acquire) ||
        s_connection.load(std::memory_order_acquire) != kNoConnection) return;
    const uint32_t attempt = s_advertising_retry_attempt.fetch_add(
        1, std::memory_order_acq_rel);
    const size_t index = attempt <
                                 sizeof(kAdvertisingRetryDelaysMs) /
                                     sizeof(kAdvertisingRetryDelaysMs[0])
                             ? attempt
                             : sizeof(kAdvertisingRetryDelaysMs) /
                                       sizeof(kAdvertisingRetryDelaysMs[0]) -
                                   1U;
    (void)esp_timer_stop(s_advertising_retry_timer);
    const esp_err_t result = esp_timer_start_once(
        s_advertising_retry_timer,
        static_cast<uint64_t>(kAdvertisingRetryDelaysMs[index]) * 1000ULL);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "failed to arm advertising retry: %s",
                 esp_err_to_name(result));
    }
}

void start_advertising_with_retry()
{
    if (!s_running.load(std::memory_order_acquire) ||
        s_stopping.load(std::memory_order_acquire) ||
        s_connection.load(std::memory_order_acquire) != kNoConnection) return;
    const int rc = start_advertising();
    if (rc == 0) {
        s_advertising_retry_attempt.store(0, std::memory_order_release);
        return;
    }
    ESP_LOGW(kTag,
             "advertising unavailable rc=%d internal_free=%u largest=%u; retrying",
             rc,
             static_cast<unsigned>(heap_caps_get_free_size(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    schedule_advertising_retry();
}

void advertising_retry_timer_callback(void *)
{
    start_advertising_with_retry();
}

void on_sync()
{
    uint8_t address_type = 0;
    if (ble_hs_util_ensure_addr(0) == 0 &&
        ble_hs_id_infer_auto(0, &address_type) == 0) {
        s_own_addr_type.store(address_type, std::memory_order_release);
        /*
         * Windows keeps a system-wide GATT cache for bonded peripherals.  If
         * the peripheral reboots while the Companion remains alive, WinRT can
         * otherwise hand the new connection the incomplete service view from
         * the interrupted session.  Service Changed is part of the standard
         * GATT service.  NimBLE persists this update for subscribed bonded
         * peers and sends it when they reconnect, causing Windows to refresh
         * its attribute table.
         */
        ble_svc_gatt_changed(0x0001, 0xffff);
        ESP_LOGI(kTag, "GATT ready status_handle=0x%04x; cache refresh queued",
                 s_status_value_handle);
        start_advertising_with_retry();
    }
}

void host_task(void *)
{
    nimble_port_run();
    s_host_exited.store(true, std::memory_order_release);
    TaskHandle_t control = s_control_task.load(std::memory_order_acquire);
    if (control) {
        xTaskNotify(control, kControlNotifyHostExited, eSetBits);
    }
    // The control task owns nimble_port_freertos_deinit().  That function
    // deletes this host task, so no code placed after it would ever run.
    vTaskSuspend(nullptr);
}

bool initialize_transport()
{
    if (s_initialized.load(std::memory_order_acquire)) return true;
    if (nimble_port_init() != ESP_OK) return false;
    const esp_timer_create_args_t timer_args = {
        .callback = first_status_watchdog,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "codex_first",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&timer_args, &s_first_status_timer) != ESP_OK) {
        nimble_port_deinit();
        return false;
    }
    const esp_timer_create_args_t advertising_timer_args = {
        .callback = advertising_retry_timer_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "codex_adv",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&advertising_timer_args,
                         &s_advertising_retry_timer) != ESP_OK) {
        (void)esp_timer_delete(s_first_status_timer);
        s_first_status_timer = nullptr;
        nimble_port_deinit();
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(kDeviceName) != 0 ||
        ble_gatts_count_cfg(kServices) != 0 ||
        ble_gatts_add_svcs(kServices) != 0) {
        (void)esp_timer_delete(s_first_status_timer);
        s_first_status_timer = nullptr;
        (void)esp_timer_delete(s_advertising_retry_timer);
        s_advertising_retry_timer = nullptr;
        nimble_port_deinit();
        return false;
    }
    ble_store_config_init();
    s_initialized.store(true, std::memory_order_release);
    return true;
}

bool start_transport_owned()
{
    if (s_running.load(std::memory_order_acquire)) return true;
    const bool initialized = initialize_transport();
    if (initialized && s_desired_enabled.load(std::memory_order_acquire)) {
        s_stopping.store(false, std::memory_order_release);
        s_running.store(true, std::memory_order_release);
        s_host_exited.store(false, std::memory_order_release);
        nimble_port_freertos_init(host_task);
        ESP_LOGI(kTag, "%s", "transport host started");
    }
    return initialized && s_running.load(std::memory_order_acquire);
}

void delete_transport_timers()
{
    if (s_first_status_timer) {
        (void)esp_timer_stop(s_first_status_timer);
        (void)esp_timer_delete(s_first_status_timer);
        s_first_status_timer = nullptr;
    }
    if (s_advertising_retry_timer) {
        (void)esp_timer_stop(s_advertising_retry_timer);
        (void)esp_timer_delete(s_advertising_retry_timer);
        s_advertising_retry_timer = nullptr;
    }
}

bool stop_transport_owned()
{
    s_stopping.store(true, std::memory_order_release);
    s_running.store(false, std::memory_order_release);
    if (s_advertising_retry_timer) {
        (void)esp_timer_stop(s_advertising_retry_timer);
    }
    s_advertising_retry_attempt.store(0, std::memory_order_release);
    clear_pairing();
    (void)ble_gap_adv_stop();
    const uint16_t connection = s_connection.load(std::memory_order_acquire);
    if (connection != kNoConnection) {
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }

    if (s_initialized.load(std::memory_order_acquire) &&
        !s_host_exited.load(std::memory_order_acquire)) {
        int rc = nimble_port_stop();
        ESP_LOGI(kTag, "host stop requested rc=%d", rc);
        while (!s_host_exited.load(std::memory_order_acquire)) {
            uint32_t notifications = 0;
            (void)xTaskNotifyWait(0, UINT32_MAX, &notifications,
                                  pdMS_TO_TICKS(kHostStopRetryMs));
            if (!s_host_exited.load(std::memory_order_acquire)) {
                rc = nimble_port_stop();
                ESP_LOGW(kTag, "host still running; stop retry rc=%d", rc);
            }
        }
        ESP_LOGI(kTag, "%s", "host event loop exited");
        nimble_port_freertos_deinit();
    }

    bool deinitialized = true;
    if (s_initialized.load(std::memory_order_acquire)) {
        const esp_err_t result = nimble_port_deinit();
        deinitialized = result == ESP_OK;
        ESP_LOGI(kTag, "transport deinit result=%s", esp_err_to_name(result));
        if (deinitialized) {
            s_initialized.store(false, std::memory_order_release);
            delete_transport_timers();
        }
    }
    reset_transport_session_state();
    s_stopping.store(false, std::memory_order_release);
    return deinitialized;
}

void reset_transport_session_state()
{
    s_connection.store(kNoConnection, std::memory_order_release);
    s_connection_secure.store(false, std::memory_order_release);
    s_status_received.store(false, std::memory_order_release);
    s_secure_since_tick.store(0, std::memory_order_release);
    codex_usage_state_connection_changed(false, false);
    codex_usage_state_reset();
    clear_pairing();
    notify_ui_task();
}

bool clear_bonds_owned()
{
    if (s_running.load(std::memory_order_acquire) ||
        s_initialized.load(std::memory_order_acquire)) {
        while (!stop_transport_owned()) {
            vTaskDelay(pdMS_TO_TICKS(kHostStopRetryMs));
        }
    }
    const esp_err_t init_result = nimble_port_init();
    if (init_result != ESP_OK) {
        ESP_LOGW(kTag, "bond-clear init failed: %s",
                 esp_err_to_name(init_result));
        return false;
    }
    ble_store_config_init();
    const bool cleared = ble_store_clear() == 0;
    const esp_err_t deinit_result = nimble_port_deinit();
    ESP_LOGI(kTag, "bond clear=%u deinit=%s", cleared,
             esp_err_to_name(deinit_result));
    reset_transport_session_state();
    return cleared && deinit_result == ESP_OK;
}

void control_task(void *)
{
    uint32_t retry_attempt = 0;
    for (;;) {
        if (s_clear_bonds_pending.exchange(false, std::memory_order_acq_rel)) {
            s_clear_bonds_result.store(clear_bonds_owned(),
                                       std::memory_order_release);
            if (s_clear_bonds_done) xSemaphoreGive(s_clear_bonds_done);
            retry_attempt = 0;
            continue;
        }
        const bool desired = s_desired_enabled.load(std::memory_order_acquire);
        const bool initialized = s_initialized.load(std::memory_order_acquire);
        const bool running = s_running.load(std::memory_order_acquire);
        const CodexBleLifecycleAction action =
            codex_ble_lifecycle_action(initialized, running, desired);
        if (action == CodexBleLifecycleAction::kStop) {
            if (running || initialized) {
                if (!stop_transport_owned()) {
                    vTaskDelay(pdMS_TO_TICKS(kHostStopRetryMs));
                    continue;
                }
            }
            retry_attempt = 0;
            continue;
        }
        if (action == CodexBleLifecycleAction::kStart) {
            if (start_transport_owned()) {
                retry_attempt = 0;
                continue;
            }
            const uint32_t delay_ms =
                codex_ble_transport_retry_delay_ms(retry_attempt++);
            ESP_LOGW(kTag, "transport start failed; retry in %u ms",
                     static_cast<unsigned>(delay_ms));
            uint32_t notifications = 0;
            (void)xTaskNotifyWait(0, UINT32_MAX, &notifications,
                                  pdMS_TO_TICKS(delay_ms));
            continue;
        }
        retry_attempt = 0;
        uint32_t notifications = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notifications, portMAX_DELAY);
    }
}

bool ensure_control_task()
{
    if (s_control_task.load(std::memory_order_acquire)) return true;
    if (s_control_task_starting.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    if (!s_clear_bonds_done) {
        s_clear_bonds_done = xSemaphoreCreateBinary();
    }
    TaskHandle_t task = nullptr;
    const BaseType_t created = s_clear_bonds_done
                                   ? xTaskCreate(control_task,
                                                 "codex_ble_ctl",
                                                 kControlTaskStackWords,
                                                 nullptr,
                                                 kControlTaskPriority,
                                                 &task)
                                   : pdFAIL;
    if (created == pdPASS) {
        s_control_task.store(task, std::memory_order_release);
    }
    s_control_task_starting.clear(std::memory_order_release);
    return created == pdPASS;
}
}

bool codex_usage_ble_request_enabled(bool enabled)
{
    s_desired_enabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        clear_pairing();
        reset_transport_session_state();
    }
    if (!enabled && !s_control_task.load(std::memory_order_acquire)) return true;
    if (!ensure_control_task()) {
        ESP_LOGW(kTag, "%s", "BLE control task unavailable");
        return false;
    }
    xTaskNotify(s_control_task.load(std::memory_order_acquire),
                kControlNotifyDesired, eSetBits);
    return true;
}

bool codex_usage_ble_clear_bonds()
{
    if (!ensure_control_task() || !s_clear_bonds_done ||
        s_clear_bonds_pending.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    (void)xSemaphoreTake(s_clear_bonds_done, 0);
    xTaskNotify(s_control_task.load(std::memory_order_acquire),
                kControlNotifyClearBonds, eSetBits);
    if (xSemaphoreTake(s_clear_bonds_done,
                       pdMS_TO_TICKS(kClearBondsTimeoutMs)) != pdTRUE) {
        return false;
    }
    return s_clear_bonds_result.load(std::memory_order_acquire);
}

bool codex_usage_ble_pairing_snapshot(CodexPairingSnapshot *out)
{
    if (!out) return false;
    out->generation = s_pairing_generation.load(std::memory_order_acquire);
    out->passkey = s_pairing_passkey.load(std::memory_order_relaxed);
    out->expires_tick_ms = s_pairing_expiry.load(std::memory_order_relaxed);
    out->visible = s_pairing_visible.load(std::memory_order_acquire);
    return true;
}

void codex_usage_ble_clear_pairing_overlay()
{
    clear_pairing();
}

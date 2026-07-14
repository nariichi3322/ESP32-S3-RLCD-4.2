// 集中维护 UI 任务句柄并安全转发跨核心刷新通知。
#include "ui_task_notify.h"

#include <atomic>

namespace {
std::atomic<TaskHandle_t> s_ui_task_handle{nullptr};
} // namespace

void register_ui_task_handle(TaskHandle_t handle)
{
    s_ui_task_handle.store(handle, std::memory_order_release);
}

void notify_ui_task()
{
    TaskHandle_t handle = s_ui_task_handle.load(std::memory_order_acquire);
    if (handle) {
        xTaskNotifyGive(handle);
    }
}

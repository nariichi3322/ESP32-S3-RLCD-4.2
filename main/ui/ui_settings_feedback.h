// 声明设置页反馈和手动网络同步状态的共享接口。
#pragma once

#include "app_state.h"

void set_settings_feedback(const char *text, uint32_t duration_ms);
bool is_settings_sync_busy();
void begin_settings_sync(SettingsSyncOp op, const char *text);
void finish_settings_sync(SettingsSyncOp op, const char *text);

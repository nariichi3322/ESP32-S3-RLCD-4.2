// 声明离线模式运行态的窄读写接口，持久化仍由网络配置模块负责。
#pragma once

bool offline_mode_enabled_load();
void offline_mode_enabled_store(bool enabled);

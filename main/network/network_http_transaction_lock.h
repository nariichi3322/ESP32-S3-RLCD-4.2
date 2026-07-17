// 声明 HTTPS/WSS 全局事务锁的初始化和跨函数所有权接口。
#pragma once

#include "freertos/FreeRTOS.h"

bool init_network_http_transaction_lock();
bool acquire_network_http_transaction_lock(TickType_t timeout);
void release_network_http_transaction_lock();

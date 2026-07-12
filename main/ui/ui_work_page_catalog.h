// 声明工作页名称、启用状态和自定义顺序的共享接口。
#pragma once

#include <stdint.h>

bool is_work_page_enabled(int page);
bool work_page_requires_network(int page);
uint8_t work_page_mask_for_offline_mode(uint8_t page_mask);
const char *work_page_name(int page);
int display_settings_item_work_page(int item);
int first_enabled_work_page();
int next_enabled_work_page(int current_page);
int first_enabled_work_page_order_index();
int next_enabled_work_page_order_index(int current_order_index);
int valid_enabled_work_page_order_index(int current_order_index);
bool work_page_mask_has_valid_home(uint8_t page_mask);
bool work_page_order_has_valid_home();
void ensure_active_work_page_enabled();
void reset_work_page_order();
void normalize_work_page_order();

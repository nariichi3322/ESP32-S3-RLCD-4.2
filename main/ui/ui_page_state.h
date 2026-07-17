// 声明页面根对象、显隐切换和工作页状态区域的公共接口。
#pragma once

#include "lvgl.h"

enum class AuxiliaryPage {
    kSystemInfo,
    kNetworkDiagnostics,
    kSettings,
    kCount,
};

lv_obj_t *work_page_root(int page);
void set_work_page_root(int page, lv_obj_t *root);
void clear_work_page_root_refs();
lv_obj_t *auxiliary_page_root(AuxiliaryPage page);
void set_auxiliary_page_root(AuxiliaryPage page, lv_obj_t *root);
void clear_auxiliary_page_root_refs();
lv_obj_t *create_page_root();
void set_page_visible(lv_obj_t *page, bool visible);
void show_page(lv_obj_t *page);
lv_obj_t *active_work_page_root();
void show_active_work_page();
void remember_lower_panel_object(lv_obj_t *obj);
void set_lower_panel_visible(bool visible);
void clear_lower_panel_object_refs();
bool set_obj_visible(lv_obj_t *obj, bool visible);
bool update_low_battery_state();
void apply_clock_mode_visibility(bool setup_active);
void update_alert_pill(bool show, int alert_index = 0);
bool update_top_status_icons(bool alert_visible);

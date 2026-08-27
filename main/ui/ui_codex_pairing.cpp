#include "ui_codex_pairing.h"

#include "app_display_config.h"
#include "app_tick_time.h"
#include "codex_usage_ble.h"
#include "ui_widgets.h"

#include "lvgl.h"

#include <stdio.h>

namespace {
lv_obj_t *s_overlay;
lv_obj_t *s_passkey;
uint32_t s_generation = UINT32_MAX;

bool ensure_overlay()
{
    if (s_overlay) return true;
    s_overlay = lv_obj_create(lv_layer_top());
    if (!s_overlay) return false;
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, kDisplayWidth, kDisplayHeight);
    lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
    make_centered_label_with_font(s_overlay, 25, 45, 350, 35,
                                  "BLUETOOTH PAIRING",
                                  &lv_font_montserrat_24,
                                  "pairing title create failed");
    make_centered_label_with_font(s_overlay, 25, 91, 350, 25,
                                  "Enter this code in Windows",
                                  &lv_font_montserrat_14,
                                  "pairing instruction create failed");
    s_passkey = make_centered_label_with_font(s_overlay, 25, 125, 350, 75,
                                               "------",
                                               &lv_font_montserrat_48,
                                               "pairing passkey create failed");
    make_centered_label_with_font(s_overlay, 25, 218, 350, 25,
                                  "Secure connection required",
                                  &lv_font_montserrat_14,
                                  "pairing security label create failed");
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    return true;
}
}

bool update_codex_pairing_overlay(uint32_t now_tick_ms)
{
    CodexPairingSnapshot pairing{};
    if (!codex_usage_ble_pairing_snapshot(&pairing)) return false;
    if (pairing.visible &&
        app_tick_deadline_remaining(now_tick_ms, pairing.expires_tick_ms) == 0) {
        codex_usage_ble_clear_pairing_overlay();
        pairing.visible = false;
    }
    if (pairing.generation == s_generation &&
        (!pairing.visible || s_overlay)) {
        if (pairing.visible && s_overlay) lv_obj_move_foreground(s_overlay);
        return false;
    }
    s_generation = pairing.generation;
    if (!pairing.visible) {
        if (!s_overlay || lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    if (!ensure_overlay()) return false;
    char passkey[7];
    snprintf(passkey, sizeof(passkey), "%06lu",
             static_cast<unsigned long>(pairing.passkey));
    bool changed = set_label_text_if_changed(s_passkey, passkey);
    if (lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        changed = true;
    }
    lv_obj_move_foreground(s_overlay);
    return changed;
}

void clear_codex_pairing_overlay_object_refs()
{
    s_overlay = nullptr;
    s_passkey = nullptr;
    s_generation = UINT32_MAX;
}

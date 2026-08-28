#include "ui_language_internal.h"

#include <assert.h>
#include <string.h>

int main()
{
    assert(normalize_ui_language(0) == UiLanguage::Traditional);
    assert(normalize_ui_language(1) == UiLanguage::Simplified);
    assert(normalize_ui_language(255) == UiLanguage::Traditional);
    assert(ui_language_load() == UiLanguage::Traditional);
    assert(strcmp(ui_language_text("設定", "设置"), "設定") == 0);
    assert(strcmp(ui_language_localize("网络检测"), "網路檢測") == 0);
    assert(strcmp(ui_language_localize("天气同步中"), "天氣同步中") == 0);
    assert(strcmp(ui_language_localize("湿度 --%"), "溼度 --%") == 0);
    ui_language_store(UiLanguage::Simplified);
    assert(ui_language_load() == UiLanguage::Simplified);
    assert(strcmp(ui_language_text("設定", "设置"), "设置") == 0);
    assert(strcmp(ui_language_localize("網路檢測"), "网络检测") == 0);
    assert(strcmp(ui_language_localize("dynamic text"), "dynamic text") == 0);
    return 0;
}

#include "ui_language_internal.h"

#include <assert.h>
#include <string.h>

int main()
{
    assert(normalize_ui_language(0) == UiLanguage::Traditional);
    assert(normalize_ui_language(1) == UiLanguage::Simplified);
    assert(normalize_ui_language(2) == UiLanguage::English);
    assert(normalize_ui_language(3) == UiLanguage::Japanese);
    assert(normalize_ui_language(255) == UiLanguage::Traditional);
    assert(ui_language_load() == UiLanguage::Traditional);
    assert(strcmp(ui_language_text("設定", "设置", "Settings", "設定"), "設定") == 0);
    assert(strcmp(ui_language_localize("网络检测"), "網路檢測") == 0);
    assert(strcmp(ui_language_localize("天气同步中"), "天氣同步中") == 0);
    assert(strcmp(ui_language_localize("湿度 --%"), "溼度 --%") == 0);
    ui_language_store(UiLanguage::Simplified);
    assert(ui_language_load() == UiLanguage::Simplified);
    assert(strcmp(ui_language_text("設定", "设置", "Settings", "設定"), "设置") == 0);
    assert(strcmp(ui_language_localize("網路檢測"), "网络检测") == 0);
    assert(strcmp(ui_language_localize("dynamic text"), "dynamic text") == 0);
    const uint32_t revision = ui_language_revision();
    ui_language_store(UiLanguage::English);
    assert(ui_language_load() == UiLanguage::English);
    assert(ui_language_revision() != revision);
    assert(strcmp(ui_language_text("設定", "设置", "Settings", "設定"), "Settings") == 0);
    assert(strcmp(ui_language_localize("天氣同步中"), "Sync weather") == 0);
    assert(strcmp(ui_language_localize("等待資料"), "Waiting for data") == 0);
    assert(strcmp(ui_language_localize("請設定 Wi-Fi"), "Set Wi-Fi") == 0);
    ui_language_store(UiLanguage::Japanese);
    assert(ui_language_load() == UiLanguage::Japanese);
    assert(strcmp(ui_language_locale_tag(UiLanguage::Japanese), "ja") == 0);
    assert(strcmp(ui_language_open_meteo_tag(), "ja") == 0);
    assert(strcmp(ui_language_text("設定", "设置", "Settings", "設定"), "設定") == 0);
    assert(strcmp(ui_language_localize("网络检测"), "ネットワーク確認") == 0);
    assert(strcmp(ui_language_localize("请继续说话"), "話し続けてください") == 0);
    assert(strcmp(ui_language_localize("正在连接Wi-Fi"), "Wi-Fiに接続中") == 0);
    UiLanguage parsed = UiLanguage::Traditional;
    assert(ui_language_from_locale_tag("ja", &parsed));
    assert(parsed == UiLanguage::Japanese);
    assert(!ui_language_from_locale_tag("ko", &parsed));
    ui_language_store(UiLanguage::Traditional);
    return 0;
}

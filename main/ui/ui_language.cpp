#include "ui_language_internal.h"

#include <atomic>
#include <string.h>

namespace {
std::atomic<uint8_t> s_language{static_cast<uint8_t>(kDefaultUiLanguage)};

struct LocalizedLiteral {
    const char *traditional;
    const char *simplified;
};

constexpr LocalizedLiteral kLocalizedLiterals[] = {
    {"網路檢測", "网络检测"}, {"準備檢測...", "准备检测..."},
    {"檢測中...", "检测中..."}, {"檢測完成", "检测完成"},
    {"等待開始", "等待开始"}, {"同步中", "同步中"},
    {"等待資料", "等待数据"}, {"天氣同步中", "天气同步中"},
    {"小智準備中", "小智准备中"},
    {"已完成", "已完成"}, {"專注中", "专注中"},
    {"分 / 秒", "分 / 秒"}, {"綁定 ID: ", "绑定 ID: "},
    {"番茄鐘", "番茄钟"}, {"設定", "设置"},
    {"KEY選擇  長按返回  BOOT確認", "KEY选择  长按返回  BOOT确认"},
    {"一言更新逾時", "一言更新超时"}, {"天氣同步逾時", "天气同步超时"},
    {"時間同步逾時", "时间同步超时"}, {"下載中，請等待", "下载中，请等待"},
    {"正在檢查，請等待", "正在检查，请等待"}, {"即將重新啟動", "即将重启"},
    {"BOOT開始檢查", "BOOT开始检查"}, {"BOOT安裝更新", "BOOT安装更新"},
    {"BOOT重新檢查", "BOOT重新检查"}, {"日出", "日出"},
    {"日落", "日落"}, {"預警 ", "预警 "},
    {"預警 --", "预警 --"}, {"溼度 --%", "湿度 --%"},
    {"等待更多天氣資料", "等待更多天气数据"},
    {"溫度", "温度"}, {"溼度", "湿度"},
    {"----/--/-- / 星期-", "----/--/-- / 星期-"},
    {"兒童節", "儿童节"}, {"聖誕", "圣诞"}, {"處暑", "处暑"},
    {"婦女節", "妇女节"}, {"勞動節", "劳动节"}, {"芒種", "芒种"},
    {"穀雨", "谷雨"}, {"閏", "闰"}, {"國慶", "国庆"},
    {"春節", "春节"}, {"情人節", "情人节"}, {"驚蟄", "惊蛰"},
    {"教師節", "教师节"}, {"臘八", "腊八"}, {"臘月", "腊月"},
    {"小滿", "小满"},
    {"儲存失敗", "保存失败"}, {"頁面順序已儲存", "页面顺序已保存"},
    {"請等待同步完成", "请等待同步完成"},
    {"目前僅開放校時網路", "目前仅开放校时网络"},
    {"此版本僅保留本機頁面", "此版本仅保留本机页面"},
    {"正在同步時間...", "正在同步时间..."}, {"已靜音", "已静音"},
    {"整點提醒已開啟", "整点提醒已开启"},
    {"整點提醒已關閉", "整点提醒已关闭"},
    {"全天提醒已開啟", "全天提醒已开启"},
    {"全天提醒已關閉", "全天提醒已关闭"},
    {"BOOT交換並儲存", "BOOT交换并保存"},
    {"頁面開關：BOOT切換", "页面开关：BOOT切换"},
    {"至少保留一個頁面", "至少保留一个页面"},
    {"請至少保留一個非小智頁面", "请至少保留一个非小智页面"},
    {"小智AI不能設為首頁", "小智AI不能设为主页"},
    {"請先取消番茄鐘", "请先取消番茄钟"},
    {"小智自動返回已開啟", "小智自动返回已开启"},
    {"小智自動返回已關閉", "小智自动返回已关闭"},
    {"預設圖片固定 24h", "默认图片固定 24h"},
    {"鬧鐘已關閉", "闹钟已关闭"},
    {"請透過小智AI設定", "请通过小智AI设置"},
    {"配網啟動失敗", "配网启动失败"},
    {"設定模式已開啟，請連線 AP", "设置模式已开启，请连接 AP"},
    {"再次按 BOOT 確認", "再次按 BOOT 确认"},
    {"恢復失敗", "恢复失败"},
    {"Codex 配對已清除", "Codex 配对已清除"},
    {"清除 Codex 配對失敗", "清除 Codex 配对失败"},
    {"已開啟", "已开启"}, {"已關閉", "已关闭"},
    {"離線模式已開啟", "离线模式已开启"},
    {"未設定 WiFi", "未配置 WiFi"},
    {"時間同步完成", "时间同步完成"}, {"時間同步失敗", "时间同步失败"},
    {"天氣同步完成", "天气同步完成"}, {"天氣同步失敗", "天气同步失败"},
    {"一言更新完成", "一言更新完成"}, {"一言更新失敗", "一言更新失败"},
    {"網路檢測完成", "网络检测完成"}, {"網路檢測已取消", "网络检测已取消"},
    {"電量低，已略過", "电量低，已跳过"},
    {"等待", "等待"}, {"檢測中", "检测中"},
    {"逾時/失敗", "超时/失败"},
    {"本機IP: --", "本地IP: --"}, {"公網IP: --", "公网IP: --"},
    {"IP定位: 未檢測", "IP定位: 未检测"}, {"DNS: 未檢測", "DNS: 未检测"},
    {"天氣: 未檢測", "天气: 未检测"}, {"NTP: 未檢測", "NTP: 未检测"},
    {"一言: 未檢測", "一言: 未检测"}, {"公網: 未檢測", "公网: 未检测"},
    {"OTA來源: 未檢測", "OTA源: 未检测"},
    {"有雨雪，出門記得帶傘。", "有雨雪，出门记得带伞。"},
    {"天氣較熱，注意防曬補水。", "天气较热，注意防晒补水。"},
    {"氣溫偏低，注意保暖。", "气温偏低，注意保暖。"},
    {"早晚溫差大，建議帶外套。", "早晚温差大，建议备外套。"},
    {"天氣平穩，適合輕裝出行。", "天气平稳，适合轻装出行。"},
};
}

UiLanguage normalize_ui_language(uint8_t stored)
{
    return stored == static_cast<uint8_t>(UiLanguage::Simplified)
               ? UiLanguage::Simplified
               : UiLanguage::Traditional;
}

UiLanguage ui_language_load()
{
    return normalize_ui_language(s_language.load(std::memory_order_acquire));
}

bool ui_language_is_traditional()
{
    return ui_language_load() == UiLanguage::Traditional;
}

const char *ui_language_text(const char *traditional, const char *simplified)
{
    const char *selected = ui_language_is_traditional() ? traditional : simplified;
    return selected ? selected : "";
}

const char *ui_language_localize(const char *text)
{
    if (!text) return "";
    for (const LocalizedLiteral &literal : kLocalizedLiterals) {
        if (strcmp(text, literal.traditional) == 0 ||
            strcmp(text, literal.simplified) == 0) {
            return ui_language_text(literal.traditional, literal.simplified);
        }
    }
    return text;
}

void ui_language_store(UiLanguage language)
{
    s_language.store(static_cast<uint8_t>(normalize_ui_language(
                         static_cast<uint8_t>(language))),
                     std::memory_order_release);
}

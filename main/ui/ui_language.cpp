#include "ui_language_internal.h"

#include <atomic>
#include <string.h>

namespace {
std::atomic<uint8_t> s_language{static_cast<uint8_t>(kDefaultUiLanguage)};
std::atomic<uint32_t> s_language_revision{1};

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
    {"日落", "日落"}, {"日出 --:--", "日出 --:--"},
    {"日落 --:--", "日落 --:--"}, {"距日落 --:--", "距日落 --:--"},
    {"預警 ", "预警 "},
    {"預警 --", "预警 --"}, {"溼度 --%", "湿度 --%"},
    {"今日 --/--°C", "今日 --/--°C"}, {"-- --級", "-- --级"},
    {"請設定 Wi-Fi", "请设置 Wi-Fi"},
    {"目前處於離線模式", "目前处于离线模式"},
    {"正在同步天氣...", "正在同步天气..."},
    {"正在更新一言...", "正在更新一言..."},
    {"正在網路檢測...", "正在网络检测..."},
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
    {"正在連線小智服務", "正在连接小智服务"},
    {"請綁定裝置", "请绑定设备"}, {"等待喚醒詞", "等待唤醒词"},
    {"小智服務無法使用", "小智服务不可用"},
    {"說出喚醒詞即可開始對話", "说出唤醒词即可开始对话"},
    {"稍後將自動重試", "稍后将自动重试"},
    {"請在小智服務中輸入綁定 ID", "请在小智服务中输入绑定 ID"},
    {"正在請求裝置綁定資訊", "正在请求设备绑定信息"},
    {"小智記憶體不足，請稍後重試", "小智内存不足，请稍后重试"},
    {"正在連線Wi-Fi", "正在连接Wi-Fi"},
    {"請先在系統設定中設定 Wi-Fi", "请先在系统设置中配置 Wi-Fi"},
    {"離線模式下無法使用小智 AI", "离线模式下无法使用小智 AI"},
    {"語音監聽初始化失敗，請稍後重試", "语音监听初始化失败，请稍后重试"},
    {"已中斷", "已打断"}, {"請繼續說話", "请继续说话"},
    {"正在聆聽", "正在聆听"}, {"請開始說話", "请开始说话"},
    {"天氣城市已設定", "天气城市已设置"},
    {"正在背景更新全部天氣", "正在后台更新全部天气"},
    {"沒有聽完整", "没有听完整"},
    {"請繼續說，或重新說一遍", "请继续说，或重新说一遍"},
    {"連線失敗，正在重試", "连接失败，正在重试"},
    {"系統繁忙，稍後重試", "系统繁忙，稍后重试"},
    {"音訊狀態異常，正在重試", "音频状态异常，正在重试"},
    {"已喚醒", "已唤醒"}, {"正在連線語音會話", "正在连接语音会话"},
    {"天氣城市已儲存", "天气城市已保存"},
    {"語音會話中斷，稍後重試", "语音会话中断，稍后重试"},
    {"小智任務啟動失敗", "小智任务启动失败"},
    {"小智正在說話", "小智正在说话"},
    {"直接說話即可打斷", "直接说话即可打断"}, {"正在對話", "正在对话"},
    {"請等待操作完成", "请等待操作完成"},
    {"天氣同步不可用", "天气同步不可用"},
    {"檢查失敗", "检查失败"}, {"正在檢查更新", "正在检查更新"},
    {"已是最新版本", "已是最新版本"}, {"下載失敗", "下载失败"},
    {"驗證失敗", "验证失败"}, {"更新失敗", "更新失败"},
    {"更新完成，正在重新啟動...", "更新完成，正在重新启动..."},
    {"沒有 Wi-Fi", "没有 Wi-Fi"}, {"電量不足", "电量不足"},
    {"Wi-Fi 失敗", "Wi-Fi 失败"}, {"沒有 OTA 分區", "没有 OTA 分区"},
    {"記憶體不足", "内存不足"}, {"更新不可用", "更新不可用"},
    {"BOOT：檢查更新", "BOOT：检查更新"},
    {"正在安裝更新 0%", "正在安装更新 0%"},
    {"正在安裝備份 0%", "正在安装备份 0%"},
    {"OTA 狀態錯誤", "OTA 状态错误"}, {"新版本 --", "新版本 --"},
};

// English translations for the still-supported two-argument compatibility
// calls. New code should use UiTextId or pass the third argument explicitly.
const char *legacy_english(const char *traditional)
{
    if (!traditional) return nullptr;
    struct Pair { const char *source; const char *english; };
    static constexpr Pair kPairs[] = {
        {"設定", "Settings"}, {"校時", "Time"}, {"聲音", "Sound"},
        {"顯示", "Display"}, {"系統", "System"}, {"同步時間", "Sync time"},
        {"同步天氣", "Sync weather"}, {"更新一言", "Update saying"},
        {"音量 靜音", "Volume muted"}, {"頁面開關", "Page toggle"},
        {"頁面順序", "Page order"}, {"自動返回", "Auto return"},
        {"離線模式", "Offline mode"}, {"關於本機", "About"},
        {"清除配對", "Clear pairing"}, {"設定模式", "Setup mode"},
        {"檢查更新", "Check for updates"}, {"網路檢測", "Network diagnostics"},
        {"時間同步完成", "Time sync complete"}, {"時間同步失敗", "Time sync failed"},
        {"天氣同步完成", "Weather sync complete"}, {"天氣同步失敗", "Weather sync failed"},
        {"等待更多天氣資料", "Waiting for more weather data"},
        {"天氣同步中", "Sync weather"}, {"準備檢測...", "Ready to check..."},
        {"檢測中...", "Checking..."}, {"檢測完成", "Check complete"},
        {"等待開始", "Waiting to start"}, {"已開啟", "Enabled"},
        {"已關閉", "Disabled"}, {"儲存失敗", "Save failed"},
        {"恢復原廠", "Factory reset"}, {"確認恢復", "Confirm reset"},
        {"日出", "Sunrise"}, {"日落", "Sunset"}, {"日出 --:--", "Sunrise --:--"},
        {"日落 --:--", "Sunset --:--"}, {"距日落 --:--", "Until sunset --:--"},
        {"今日 --/--°C", "Today --/--°C"}, {"-- --級", "-- level --"},
        {"請設定 Wi-Fi", "Set Wi-Fi"}, {"溫度", "Temperature"},
        {"溼度", "Humidity"}, {"預警", "Warning"}, {"等待", "Waiting"},
        {"檢測中", "Checking"}, {"逾時/失敗", "Timeout/failed"},
        {"同步中", "Syncing"}, {"等待資料", "Waiting"},
        {"小智準備中", "Xiaozhi preparing"}, {"已完成", "Completed"},
        {"專注中", "Focusing"}, {"分 / 秒", "min / sec"},
        {"綁定 ID: ", "Binding ID: "}, {"番茄鐘", "Pomodoro"},
        {"一言更新逾時", "Saying update timed out"}, {"天氣同步逾時", "Weather sync timed out"},
        {"時間同步逾時", "Time sync timed out"}, {"下載中，請等待", "Downloading, please wait"},
        {"正在檢查，請等待", "Checking, please wait"}, {"即將重新啟動", "Restarting soon"},
        {"BOOT開始檢查", "BOOT: start check"}, {"BOOT安裝更新", "BOOT: install update"},
        {"BOOT重新檢查", "BOOT: check again"}, {"預警 ", "Warning "},
        {"預警 --", "Warning --"}, {"溼度 --%", "Humidity --%"},
        {"----/--/-- / 星期-", "----/--/-- / ---"},
        {"頁面順序已儲存", "Page order saved"}, {"請等待同步完成", "Wait for sync to complete"},
        {"目前僅開放校時網路", "Only time-sync network is available"},
        {"此版本僅保留本機頁面", "This version keeps local pages only"},
        {"正在同步時間...", "Synchronizing time..."}, {"已靜音", "Muted"},
        {"整點提醒已開啟", "Hourly reminder enabled"}, {"整點提醒已關閉", "Hourly reminder disabled"},
        {"全天提醒已開啟", "All-day reminder enabled"}, {"全天提醒已關閉", "All-day reminder disabled"},
        {"至少保留一個頁面", "Keep at least one page"},
        {"請至少保留一個非小智頁面", "Keep at least one non-Xiaozhi page"},
        {"小智AI不能設為首頁", "Xiaozhi AI cannot be the home page"},
        {"請先取消番茄鐘", "Cancel Pomodoro first"},
        {"小智自動返回已開啟", "Xiaozhi auto-return enabled"},
        {"小智自動返回已關閉", "Xiaozhi auto-return disabled"},
        {"預設圖片固定 24h", "Default image fixed at 24h"},
        {"鬧鐘已關閉", "Alarm disabled"}, {"請透過小智AI設定", "Set this through Xiaozhi AI"},
        {"配網啟動失敗", "Setup mode failed to start"},
        {"設定模式已開啟，請連線 AP", "Setup mode enabled; connect to the AP"},
        {"再次按 BOOT 確認", "Press BOOT again to confirm"}, {"恢復失敗", "Reset failed"},
        {"離線模式已開啟", "Offline mode enabled"}, {"未設定 WiFi", "Wi-Fi is not configured"},
        {"一言更新完成", "Saying update complete"}, {"一言更新失敗", "Saying update failed"},
        {"網路檢測完成", "Network diagnostics complete"}, {"網路檢測已取消", "Network diagnostics cancelled"},
        {"電量低，已略過", "Skipped: battery low"}, {"本機IP: --", "Local IP: --"},
        {"公網IP: --", "Public IP: --"}, {"天氣: 未檢測", "Weather: not checked"},
        {"NTP: 未檢測", "NTP: not checked"}, {"公網: 未檢測", "Internet: not checked"},
        {"請由設定網頁或小智修改", "Change this from the setup page or Xiaozhi"},
        {"聲音 %d", "Sound %d"}, {"自訂圖 %s", "Custom image %s"},
        {"目前處於離線模式", "Currently offline"},
        {"正在同步天氣...", "Synchronizing weather..."},
        {"正在更新一言...", "Updating daily saying..."},
        {"正在網路檢測...", "Running network diagnostics..."},
        {"請先完成線上設定", "Complete online setup first"},
        {"Codex 配對已清除", "Codex pairing cleared"},
        {"清除 Codex 配對失敗", "Failed to clear Codex pairing"},
        {"已切換語言", "Language changed"}, {"鬧鐘 --:--", "Alarm --:--"},
        {"自訂圖", "Custom image"}, {"開", "On"}, {"關", "Off"},
        {"正在連線小智服務", "Connecting to Xiaozhi service"},
        {"請綁定裝置", "Bind your device"}, {"等待喚醒詞", "Waiting for wake word"},
        {"小智服務無法使用", "Xiaozhi service unavailable"},
        {"說出喚醒詞即可開始對話", "Say the wake word to start a conversation"},
        {"稍後將自動重試", "Retrying later"},
        {"請在小智服務中輸入綁定 ID", "Enter the binding ID in Xiaozhi"},
        {"正在請求裝置綁定資訊", "Requesting device binding information"},
        {"小智記憶體不足，請稍後重試", "Xiaozhi is out of memory. Try again later."},
        {"正在連線Wi-Fi", "Connecting to Wi-Fi"},
        {"請先在系統設定中設定 Wi-Fi", "Configure Wi-Fi in system settings first"},
        {"離線模式下無法使用小智 AI", "Xiaozhi AI is unavailable offline"},
        {"語音監聽初始化失敗，請稍後重試", "Voice listener initialization failed. Try again later."},
        {"已中斷", "Interrupted"}, {"請繼續說話", "Please continue speaking"},
        {"正在聆聽", "Listening"}, {"請開始說話", "Please start speaking"},
        {"天氣城市已設定", "Weather city set"},
        {"正在背景更新全部天氣", "Updating all weather in the background"},
        {"沒有聽完整", "I did not hear that completely"},
        {"請繼續說，或重新說一遍", "Please continue or say it again"},
        {"連線失敗，正在重試", "Connection failed; retrying"},
        {"系統繁忙，稍後重試", "System busy; try again later"},
        {"音訊狀態異常，正在重試", "Audio state error; retrying"},
        {"已喚醒", "Awake"}, {"正在連線語音會話", "Connecting to voice session"},
        {"天氣城市已儲存", "Weather city saved"},
        {"語音會話中斷，稍後重試", "Voice session interrupted; retrying"},
        {"小智任務啟動失敗", "Failed to start Xiaozhi task"},
        {"小智正在說話", "Xiaozhi is speaking"},
        {"直接說話即可打斷", "Speak directly to interrupt"}, {"正在對話", "In conversation"},
        {"請等待操作完成", "Please wait for the current operation to finish"},
        {"天氣同步不可用", "Weather sync is unavailable"},
        {"檢查失敗", "Check failed"}, {"正在檢查更新", "Checking update"},
        {"已是最新版本", "Already latest"}, {"下載失敗", "Download failed"},
        {"驗證失敗", "Verify failed"}, {"更新失敗", "Update failed"},
        {"更新完成，正在重新啟動...", "Update done. Rebooting..."},
        {"沒有 Wi-Fi", "No Wi-Fi"}, {"電量不足", "Low battery"},
        {"Wi-Fi 失敗", "Wi-Fi failed"}, {"沒有 OTA 分區", "No OTA partition"},
        {"記憶體不足", "No memory"}, {"更新不可用", "Update unavailable"},
        {"BOOT：檢查更新", "BOOT: Check Update"},
        {"正在安裝更新 0%", "Installing update 0%"},
        {"正在安裝備份 0%", "Installing backup 0%"},
        {"OTA 狀態錯誤", "OTA status error"}, {"新版本 --", "New version --"},
        {"KEY選擇  長按返回  BOOT確認", "KEY select  Hold to return  BOOT confirm"},
        {"兒童節", "Children's Day"}, {"聖誕", "Christmas"}, {"處暑", "End of Heat"},
        {"婦女節", "Women's Day"}, {"勞動節", "Labour Day"}, {"芒種", "Grain in Ear"},
        {"穀雨", "Grain Rain"}, {"閏", "Leap"}, {"國慶", "National Day"},
        {"春節", "Spring Festival"}, {"情人節", "Valentine's Day"},
        {"驚蟄", "Awakening of Insects"}, {"教師節", "Teachers' Day"},
        {"臘八", "Laba"}, {"臘月", "Twelfth lunar month"}, {"小滿", "Grain Full"},
        {"BOOT交換並儲存", "BOOT swap and save"},
        {"頁面開關：BOOT切換", "Page toggle: BOOT switch"},
        {"IP定位: 未檢測", "IP location: not checked"},
        {"DNS: 未檢測", "DNS: not checked"}, {"一言: 未檢測", "Saying: not checked"},
        {"OTA來源: 未檢測", "OTA source: not checked"},
        {"有雨雪，出門記得帶傘。", "Rain or snow is expected. Remember an umbrella."},
        {"天氣較熱，注意防曬補水。", "Hot weather. Use sun protection and stay hydrated."},
        {"氣溫偏低，注意保暖。", "Cool temperatures. Dress warmly."},
        {"早晚溫差大，建議帶外套。", "Large temperature swings. Bring a jacket."},
        {"天氣平穩，適合輕裝出行。", "Stable weather. Light clothing should be comfortable."},
    };
    for (const Pair &pair : kPairs) {
        if (strcmp(traditional, pair.source) == 0) return pair.english;
    }
    return nullptr;
}
}

UiLanguage normalize_ui_language(uint8_t stored)
{
    switch (stored) {
    case static_cast<uint8_t>(UiLanguage::Simplified):
        return UiLanguage::Simplified;
    case static_cast<uint8_t>(UiLanguage::English):
        return UiLanguage::English;
    default:
        return UiLanguage::Traditional;
    }
}

UiLanguage ui_language_load()
{
    return normalize_ui_language(s_language.load(std::memory_order_acquire));
}

bool ui_language_is_traditional()
{
    return ui_language_load() == UiLanguage::Traditional;
}

bool ui_language_is_simplified()
{
    return ui_language_load() == UiLanguage::Simplified;
}

bool ui_language_is_english()
{
    return ui_language_load() == UiLanguage::English;
}

const char *ui_language_text(const char *traditional,
                             const char *simplified,
                             const char *english)
{
    const UiLanguage language = ui_language_load();
    const char *selected = traditional;
    if (language == UiLanguage::Simplified) {
        selected = simplified;
    } else if (language == UiLanguage::English) {
        const char *legacy = legacy_english(traditional);
        selected = english ? english : (legacy ? legacy : traditional);
    }
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
    const uint8_t normalized = static_cast<uint8_t>(normalize_ui_language(
        static_cast<uint8_t>(language)));
    const uint8_t previous = s_language.exchange(normalized,
                                                  std::memory_order_acq_rel);
    if (previous != normalized) {
        s_language_revision.fetch_add(1, std::memory_order_acq_rel);
    }
}

uint32_t ui_language_revision()
{
    return s_language_revision.load(std::memory_order_acquire);
}

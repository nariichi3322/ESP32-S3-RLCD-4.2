# 天氣時鐘使用手冊

## 線上與離線模式

設定模式提供兩條獨立路徑。線上使用時，填寫主要 Wi-Fi，並可選填備用 Wi-Fi、NTP 伺服器與天氣城市。線上設定儲存成功後會退出離線模式，並排程時間、天氣與每日一言同步。

離線使用時，填寫目前的本地日期時間。裝置會持久化離線模式、關閉 Wi-Fi、阻擋背景網路工作，並隱藏需要網路的頁面。若裝置已保存有效 Wi-Fi，可直接關閉離線模式；否則會進入設定模式。

## 天氣

天氣資料由 Open-Meteo 提供，不需要 API Key 或自訂 Host。手動填寫城市時，裝置使用 Open-Meteo Geocoding API 解析；城市留空時，裝置以公開 IP 取得座標，並直接使用座標查詢 Open-Meteo。

天氣看板顯示目前天氣、六日預報及目前空氣品質。此整合不提供天氣預警。天氣狀態依 WMO interpretation code 顯示供應商中立的單色圖示。

資料來源：[Open-Meteo Forecast API](https://open-meteo.com/en/docs) 與 [Geocoding API](https://open-meteo.com/en/docs/geocoding-api)。空氣品質包含透過 [Open-Meteo Air Quality API](https://open-meteo.com/en/docs/air-quality-api) 提供的 CAMS 衍生資料。重新散布韌體或含天氣資訊的畫面時，請保留上述 attribution。

## CODEX Usage 藍牙

CODEX 頁由通用頁面顯示／順序設定管理。只有正常顯示 CODEX 頁時才會啟動藍牙；離開該頁、進入低電量、設定或其他輔助頁時會停止藍牙，並清除連線與配對提示狀態。藍牙狀態圖示也只會顯示在 CODEX 頁。

需要改與其他客戶端配對時，使用「系統 > 清除 CODEX 配對」。

## 系統選單

系統選單固定包含：離線模式、恢復出廠、關於本機、清除 CODEX 配對、語言、設定模式、OTA、網路檢測。網路檢測會檢查 Open-Meteo 公開端點，以及 Wi-Fi、DNS、NTP、每日一言、公網和 OTA 來源。

## 升級

此版本第一次啟動時，會冪等刪除舊天氣憑據與舊 CODEX 獨立功能開關的 NVS 值。Wi-Fi、天氣城市、頁面顯示／順序、語言、鬧鐘及其他設定都會保留，不需要恢復出廠。

# Codex Usage 頁面 Migration 執行計畫

## 1. 目標與完成定義

在目前 ESP32-S3 RLCD 4.2 的「本地頁面 + NTP」產品模式中加入第八個工作頁「Codex Usage」。Windows 電腦上的 Companion 從本機 Codex app-server 與 hooks 取得使用量，再透過具備加密、MITM 與 bonding 的 BLE 將唯讀狀態傳給裝置。ESP32 不保存 Codex 登入資訊，也不提供 `focus_codex`、`refresh`、`new_task` 或任意電腦控制命令。

完成時必須同時滿足：

- Codex Usage 可在頁面設定中啟用、停用及排序，既有設定可無損升級。
- Windows Companion 能完成安全配對並傳送剩餘額度、重置倒數、今日／七日 Token、Reset credits 與執行中任務數。
- 400×300 RLCD 畫面可顯示 `DISCONNECT`、`WAITING`、`LINKED`、`STALE` 與有效資料，無 480×480 AMOLED 版面或觸控相依。
- 關閉 Codex Usage 頁面時不啟動 BLE；啟用時 BLE 可在 Wi-Fi 關閉及短暫 NTP 同步兩種狀態下正常工作。
- 隱藏頁面不重繪；heartbeat 未造成可見文字變化時不刷新；倒數最多每分鐘更新一次。
- 完成 host tests、Companion Python tests、來源檔／局部編譯與 App 連結尺寸檢查，不執行完整韌體映像建置或刷機。

## 2. 固定範圍與預設決策

### 納入範圍

- 來源專案：`C:\Users\sammy.shiling\Workspace\Project\exp\ESP32\esp32-c6-touch-amoled-2.16\codex-usage-display`。
- 目標平台：ESP32-S3-WROOM-1-N16R8、ESP-IDF 5.5.x、LVGL 8.4、400×300 RLCD。
- Host 平台：Windows，Python Companion 以前景 PowerShell 或使用者登入後的 Scheduled Task 執行。
- 傳輸方向：Windows → ESP32 狀態快照；ESP32 除 GATT／安全配對必要回應外不傳送應用命令。
- BLE 在 Codex Usage 頁面「已啟用」期間常駐，不以頁面是否正在顯示作為斷線條件。
- 新安裝與舊版設定升級後，Codex Usage 預設啟用並排在既有七頁之後；使用者可在顯示設定中關閉或重新排序。
- Codex 快照只保存在 RAM。重新開機且 BLE 尚未連線時顯示 `DISCONNECT`，不從 NVS 恢復舊使用量。

### 排除範圍

- macOS／Linux Companion 正式支援。
- `focus_codex`、`refresh`、`new_task`、快捷操作 overlay 或從裝置控制電腦。
- 來源 AMOLED 的觸控、喚醒、螢幕關閉、電源管理及板級驅動。
- 來源 `font_montserrat_100.c`；使用目標專案現有字型，避免增加約 137 KB 字型資源。
- Wi-Fi 或雲端直接查詢 Codex、在裝置保存 Cookie／API key／Codex 憑證。
- 完整 firmware build、OTA／merged image 產製、刷機及實機自動操作。

### 相容性與授權

- 沿用來源 BLE service UUID 與 status characteristic UUID；Companion 傳送 protocol v2 雙額度資料，韌體相容讀取 v1；不建立 command/result characteristic。
- Companion 改為 status-only，不再訂閱 command characteristic，也不保留 action 執行路徑。
- 搬入的 MIT 程式碼保留來源 copyright／license，並在本專案 `THIRD_PARTY_NOTICES.md` 加入 Codex Usage Display 與 Bleak 聲明；不改變本專案既有授權對原始碼的適用範圍。
- 執行前先檢查工作樹；目前 `main/input/input_tasks.cpp`、`main/ui/ui_page_state.cpp`、`main/ui/ui_views.cpp` 等已有使用者修改。所有整合必須逐段合併，不得覆寫或回退既有變更。

## 3. 對外介面與資料契約

### 工作頁介面

- 在 `work_page_ids.h` 追加 `kWorkPageCodexUsage = 7`，將 `kWorkPageCount` 改為 8；既有 0～6 ID 不變。
- 主頁名稱為 `Codex Usage`，設定清單使用不溢位的短名稱 `Codex`；traits 為「本地／不需要 Wi-Fi」及「低刷新頁面」。它必須保留在 `work_page_mask_for_offline_mode()` 的結果中，不觸發 weather、daily saying 或任何 Wi-Fi data requirement。
- 目前 `uint8_t` page mask 剛好容納八頁，本次維持格式；加入 static assertion 說明八頁為上限，未來第九頁必須先升級 mask 型別。
- 新增 UI 介面：`build_codex_usage_page()`、`update_codex_usage_page(const tm &, const CodexUsageSnapshotView &)`、`clear_codex_usage_page_object_refs()`；只有 UI task 可以建立或更新 LVGL object。

### 狀態模型

新增獨立 `main/codex/` 模組，分成 protocol、state、BLE transport，避免 UI、BLE callback 與 Companion wire format 互相耦合。公開給 UI 的 immutable snapshot 至少包含：

- `remaining_percent: uint8_t`，範圍 0～100。
- `tokens_today: uint64_t`、`tokens_today_estimated: bool`。
- `tokens_7d: uint64_t`。
- `active_threads: uint8_t`。
- `reset_credits: uint8_t`。
- `quota_reset_seconds: uint32_t`。
- 第二額度視窗的 available、remaining percent、reset seconds 與 window minutes。
- `next_credit_expiry_seconds: uint32_t`。
- `limit_window_minutes: uint32_t`。
- `unix_time: uint64_t`、`utc_offset_minutes: int16_t`。
- `sequence: uint32_t`、最後有效封包的 monotonic tick、資料 generation。
- `data_valid`、BLE connected/bonded 狀態，以及衍生的 `Disconnected / Waiting / Linked / Stale` UI 狀態。

狀態採單一 mutex 保護的 copy-in/copy-out store；BLE callback 驗證並提交完整 snapshot，UI 只讀副本，不直接讀 callback-owned memory。每次有效資料變更增加 generation；相同顯示值的 heartbeat 只更新 watchdog，不強迫 UI redraw。

### BLE status JSON v2（相容讀取 v1）

接受上限 180 bytes 的 UTF-8 JSON，Companion payload 也必須維持 180 bytes 內。欄位如下：

```json
{"v":2,"s":42,"t":1784341234,"o":480,"r":68,"u":300,"q":9600,"n":1,"R":74,"U":10080,"Q":358400,"d":1250000,"e":0,"w":6840000,"c":2,"x":358400,"a":3}
```

- v2 必填：既有 v1 欄位及 `n`、`R`、`U`、`Q`；`n` 表示第二視窗是否可用，韌體仍接受缺少這四欄的 v1。
- `v` 必須為 1 或 2，`s` 必須大於 0，百分比必須在 0～100；數字必須為整數且不得負值，只有 `o` 可為負並限制在合理 UTC offset 範圍 -840～840 分鐘。
- 每次 BLE connection 將 last sequence 歸零；小於 last sequence 的封包拒絕，相同 sequence 視為 idempotent heartbeat，大於 last sequence 才替換業務資料。
- malformed、超長、版本錯誤或越界 payload 不得覆蓋最後有效資料，只記錄節流後的診斷訊息。
- Companion 連線後先重新讀取 Codex，再送第一包；之後每 15 秒 heartbeat、每 60 秒重新取得帳戶資料，hook 狀態變更可立即推送。

### 連線狀態定義

- `DISCONNECT`：BLE 未連線；斷線後立即顯示，已有 snapshot 時仍保留最後資料。
- `WAITING`：BLE 已連線，但尚未完成安全配對或尚未收到有效 snapshot。
- `LINKED`：BLE 已完成安全配對，且最後有效 status 未超過 60 秒。
- `STALE`：BLE 仍連線，但超過 60 秒沒有有效 status；保留並顯示最後資料。
- 配對碼不是工作頁狀態。NimBLE callback 將六位數 passkey 發成 app event，由 UI task 顯示全畫面最高層 transient overlay；成功、失敗或 75 秒 timeout 後關閉。

## 4. 實作步驟

### A. 建立可測試的資料核心

1. 從來源 `app_state`、status parser 與 Companion protocol 抽取資料語意，不複製 Arduino `String`、ArduinoJson 或板級程式。
2. 實作 bounded JSON parser、欄位驗證、sequence 規則、countdown 飽和扣減、compact token 格式（例如 `999`、`1.2K`、`6.8M`）與連線狀態推導。
3. 實作 thread-safe snapshot store、generation 與 watchdog；所有時間差使用 monotonic tick，避免 NTP 校時造成 stale/countdown 跳動。
4. 先建立 host tests 覆蓋正常、缺欄位、越界、錯誤版本、sequence rollback、重複 heartbeat、斷線、60 秒 stale、倒數歸零及 `uint64_t` token formatting。

### B. 擴充第八頁與設定儲存

1. 追加 page ID、descriptor、預設 order、enabled mask、page root、status/battery/progress 固定陣列及所有 `kWorkPageCount` 對應表。
2. 將 NVS page settings 版本由 v5 升到 v6：讀取七頁 mask/order 時保留原七頁選擇及順序，在尾端追加並啟用 Codex Usage；寫回一律使用八頁格式。v4 → v5 → v6 必須可連續遷移。
3. 更新顯示設定的頁面開關與排序；任何「至少保留一個首頁」規則維持現況，Codex Usage 不可單獨成為 home 的限制若目前 policy 對非時鐘頁適用，則沿用同一限制。
4. 新增 `codex_usage_feature_enabled()` 判斷，設定提交後通知 BLE runtime：由 disabled → enabled 啟動，enabled → disabled 停止 advertising/connection、釋放 transport 資源並清除 transient pairing UI。
5. 更新 catalog、storage policy、active page、settings navigation、battery/progress、page roots 等 host tests，明確驗證 bit 7，不再把 `0x80` 當未知 bit。

### C. 重新設計 400×300 RLCD 頁面

頁面沿用現有白底、黑色文字、共用頂部狀態列與局部刷新框架，不移植 AMOLED 色彩、圓形 arc、觸控按鈕或 overlay actions。

- 頂部 0～63 px：沿用 work-page status bar 與分隔線；日期／時間更新沿用既有機制。
- 主內容 18～382 px、70～282 px：左側上下顯示 primary/secondary `CODEX LEFT`、百分比及 reset countdown，週期依服務回傳顯示為 `5h`、`7d`、`30d` 等；右側以兩欄／三列顯示 `TODAY`、`7 DAYS` token、`RUN`、credits／expiry。
- 底部或右下角使用邊框／反白 badge 顯示四種連線狀態，不依賴顏色傳達狀態。
- `tokens_today_estimated` 為真時在 TODAY 值前顯示 `~`。
- 無資料時各 metric 顯示 `--`；STALE 保留最後值並顯示狀態，不清空。
- 文字過長必須使用固定 buffer、compact formatting 或截斷，不允許水平捲動。
- 每個可變 label 保留上次顯示文字；只有字串改變才 set text/invalidate。只有 Codex 頁可見時執行 metric redraw，隱藏期間僅累積 generation。
- reset/expiry countdown 依接收時的 seconds 與 monotonic elapsed 計算，向零飽和；UI 排程只需分鐘邊界或新 generation 喚醒，不加入秒級常駐刷新。
- 將此頁加入 SDL preview；以 waiting、linked-normal、linked-large-values、estimated-today、stale、pairing-code 六種 fixture 產生預覽，確認沒有裁切與重疊。

### D. ESP-IDF NimBLE transport

1. 在 `sdkconfig.defaults` 與當前 `sdkconfig` 啟用 Bluetooth controller、NimBLE、peripheral role、單一連線及 bonding；停用不需要的 Classic Bluetooth／central role，實際 symbol 使用 ESP-IDF 5.5.3 提供的名稱。
2. 建立 device name `Codex Display`，沿用 service UUID `7d8b6c20-8f6d-4b44-a0f8-1b6570c0de01` 與 encrypted-write status UUID `...de02`；可保留 read-only device-info UUID `...de05` 提供 protocol/firmware 資訊，但不得建立 command/result characteristic。
3. 安全設定固定為 LE Secure Connections、bonding、MITM、display-only IO capability 與隨機六位 passkey。未加密／未 authenticated 的 status write 必須由 attribute permission 或 callback 明確拒絕。
4. BLE callback 只做 bounded copy、解析／提交與 event notify，不呼叫 LVGL、不執行阻塞 I/O。斷線後重新 advertising；重連 backoff 由 Companion 控制。
5. 啟動時讀取 settings mask 後才決定是否初始化 BLE。NTP Wi-Fi 視窗不得銷毀 NimBLE；驗證共存後，維持 BLE 連線或在受控斷線後自動恢復。
6. 新增本機設定動作「清除 Codex BLE 配對」：停止 advertising、刪除所有 Codex service bonds、清除 snapshot、重新 advertising；不得連帶清除 Wi-Fi、RTC 或其它 NVS 設定。
7. BLE 初始化／配置失敗不能阻止時鐘啟動；頁面顯示 WAITING，錯誤寫入節流 log，後續以有限 backoff 重試。

### E. Windows Companion 納入目前 repo

1. 將來源的必要 Python package、hooks、測試、`run.ps1`、requirements 與 Windows Scheduled Task installer 複製到目前 repo 的 `companion/`；不使用 symlink 或執行期依賴來源 repo 路徑。
2. 移除 macOS LaunchAgent、AppleScript action 與 command/result 處理；`BleCompanion` 只掃描指定 service、以 `pair=True` 連線、refresh 後寫入 status、維持 heartbeat 並處理重連。
3. 保留 Codex app-server、metrics、local usage 與 hooks 的資料取得邏輯；hooks 安裝器必須保存使用者既有 hooks、可重複執行且可獨立卸載。
4. Windows launcher 使用 repo 內 `.venv`，Scheduled Task 以目前使用者、limited privilege、登入後啟動、異常時重啟；log 寫入 `%LOCALAPPDATA%\CodexUsageDisplay\companion.log` 並維持 bounded rotation。
5. 更新 README/User 文件：安裝、首次配對、前景診斷、背景安裝、狀態查詢、解除安裝、清除 bonds、Windows Bluetooth 權限及已知限制。
6. 保留 payload size、Windows client options、重連、cached snapshot、hook、metrics、installer idempotency 等測試；移除反向 action 測試並新增「沒有 command characteristic 也能完成 connection」測試。

### F. 啟動、排程與低功耗整合

1. 將 Codex state／BLE runtime 初始化加入既有 startup table，順序在 NVS／page settings 完成後、UI 可接收 pairing event 前；失敗採可降級初始化。
2. 使用既有 app event/task notification 喚醒 UI。新 snapshot、BLE state change、pairing overlay change 才發通知，15 秒相同 heartbeat 不反覆喚醒 UI。
3. 在 `ui_loop_schedule` 加入 Codex 候選 deadline：頁面可見時取新 generation、stale deadline 或下一分鐘 countdown；頁面隱藏時不提供分鐘 deadline，但連線狀態變化仍保留供再次顯示時同步。
4. Codex Usage 標記 low-refresh；input idle/light-sleep policy 沿用其它靜態頁。BLE controller 的 power-management 限制由 ESP-IDF 管理，不新增永久 CPU frequency lock。
5. 低電量、Settings、配網與 pairing overlay 的顯示優先序沿用目前 auxiliary/special page 規則；不讓 Codex page 強制跳到前景，也不因 RUN 增加自動喚醒螢幕。

## 5. 測試與驗收

### 自動測試

- Protocol/state host tests：完整與錯誤 payload、邊界值、sequence、stale、disconnect、countdown、formatting、generation 去重。
- Page/catalog/storage host tests：八頁 mask/order、v4/v5 → v6 migration、offline mask 包含 Codex、bit 7、關閉後 fallback、至少一個 home。
- UI scheduling tests：隱藏頁不分鐘喚醒、可見頁在分鐘/stale deadline 更新、相同 heartbeat 不 redraw。
- Companion：執行 `python -m unittest discover -s companion/tests -v`，所有測試通過。
- SDL preview：六種 fixture 均為 400×300，人工檢查字型、邊界、黑白辨識與局部更新區域。
- 只執行受影響 host tests、受影響 C++ translation unit 編譯及最終 App link/size；不執行 merged image、flash 或完整 release build。

### 基本韌體檢查

- CMake source list、include dependency 與 ESP-IDF NimBLE symbols 可解析。
- App link 成功且 `weather_clock.bin` 小於單一 `0x6C0000` App partition；至少保留 10% App partition 餘量，否則先移除非必要資源而不是調整 partition table。
- 比較 link map 的 internal DRAM/IRAM；不得出現 overflow，BLE 啟動失敗路徑必須可降級回本地時鐘。
- 不因第八頁造成 shift overflow、`0x80` 被誤判未知、array/static assertion 或 NVS order size 錯誤。

### 實機驗收清單（需使用者操作／回報）

1. 未配對開機：本地頁正常，Codex 頁顯示 DISCONNECT，Windows 能發現 `Codex Display`。
2. 首次配對：裝置顯示六位 passkey，Windows 輸入相同碼後收到第一包並顯示 LINKED。
3. 指標比對：頁面 remaining、reset、TODAY、7 DAYS、RUN、credits/expiry 與 Companion `--once` 輸出一致。
4. 關閉／重開 Companion：BLE 斷線立即進入 DISCONNECT、保留最後值，重連後回 LINKED；只有 BLE 保持連線但資料超過 60 秒才顯示 STALE。
5. Windows Bluetooth off/on、電腦睡眠喚醒及登出登入：Scheduled Task 能恢復掃描與資料更新。
6. 執行 NTP 同步：Wi-Fi 視窗結束後關閉 Wi-Fi，BLE 保持或自動恢復，時鐘與 RTC 行為不退化。
7. 關閉 Codex Usage 頁面：BLE 停止 advertising/connection；重新啟用後恢復服務。
8. 清除 Codex 配對：不清除 Wi-Fi/NTP/RTC，Windows 可重新配對。
9. 連續運行至少 2 小時：無 watchdog、heap 持續下降、重複全屏刷新或每 15 秒可見閃動。

## 6. 執行順序與停止條件

實作必須依 A → B → C → D → E → F 的順序進行；每階段先完成對應純邏輯測試，再接下一層。預期一次完整 Migration 約使用 220k～450k tokens；若 Windows 實機配對或 RAM 問題需要多輪回報，可能增加至 450k～650k。

只有以下情況需要暫停並向使用者確認，不可自行擴張範圍：

- 必須修改 partition table、移除既有功能或永久開啟 Wi-Fi 才能容納／運作。
- NimBLE 與現有 ESP-IDF 5.5.3 元件產生無法局部解決的版本衝突。
- 需要變更既有按鍵語意、清除使用者 NVS，或覆寫目前工作樹中的既有修改。
- Windows 實機不接受 display-only passkey，必須降低到無 MITM／未加密連線；此情況不得降低安全性，應停止並提出替代配對方案。

交付時需列出：修改檔案、通過的測試、未執行項目、App/DRAM/IRAM 檢查結果、實機待驗項目及任何偏離本計畫的原因。

# Codex / Claude 用量 Windows 伴侶程式

> **語言：** [English](README.md) · 繁體中文（目前）

這是只支援 Windows 的伴侶程式（companion），透過一條加密 BLE 連線，把 AI 用量送到顯示器的 **Codex** 與 **Claude** 頁面：

- **Codex**：啟動本機的 `codex app-server`，讀取帳號的實際額度與 UTC 每日用量，並追蹤執行中的工作。
- **Claude Code**：5 小時／7 天額度、今日與 7 天 token 總量、執行中的工作階段數。

兩者可以只用其中一個：沒裝 Codex 時，companion 只送 Claude 資料；沒設定 Claude Code 時，Claude 頁顯示 `--`。companion 沒有對裝置下指令的通道，也不會把 Codex 或 Claude 的登入憑證送到 ESP32。

## 需求

- Windows 10/11，並開啟藍牙（Bluetooth LE）。
- Python **3.10 以上**（從 [python.org](https://www.python.org/) 安裝，勾選「Add python.exe to PATH」）。
- 顯示器燒錄含 Claude 頁的韌體。舊韌體沒有 Claude 特徵值，companion 會自動只送 Codex 資料。
- 使用 Codex：Codex CLI 或 OpenAI ChatGPT/Codex VS Code 擴充功能，且已登入。找不到執行檔時，可用 `CODEX_BIN` 指定路徑。
- 使用 Claude：Claude Code（CLI 或 VS Code 擴充功能）已登入。額度只有 **Pro/Max** 訂閱才有；token 統計任何登入方式都可以。

## 新電腦快速安裝

以下指令都在 repo 根目錄、用一般權限（**不要用系統管理員**）的 PowerShell 執行。

1. **取得程式碼。** clone 之後不要再搬動資料夾：安裝程式會把絕對路徑寫進設定，搬動後要重做第 4 步。

   ```powershell
   git clone https://github.com/nariichi3322/ESP32-S3-RLCD-4.2.git
   cd ESP32-S3-RLCD-4.2
   ```

2. **建立環境並確認讀得到資料**（不需要藍牙）：

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\run.ps1 --once
   ```

   第一次執行會建立 `companion\.venv` 並安裝 `bleak`。成功時會印出 v3 JSON：有 Codex 時印一行 Codex、一行 Claude；沒有 Codex 時只印 Claude。

3. **配對一次。** 先把顯示器切到 **Codex 或 Claude 頁**，藍牙只在這兩頁開啟。接著執行：

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\run.ps1
   ```

   在 Windows 跳出的配對視窗輸入顯示器上的 6 位數密碼。log 出現 `Codex Display LINKED` 後按 `Ctrl+C` 結束：同一時間只能跑一個 companion，第 5 步會自己啟動一個。

4. **安裝 Claude Code 整合**（不用 Claude 可略過）：

   ```powershell
   companion\.venv\Scripts\python.exe companion\install_claude.py
   ```

   裝完要重開 VS Code 或 Claude Code，新的 hooks 與 mod 才會載入。使用 Codex 的話，也可以安裝 Codex hooks（見 [Codex hooks 與本機 token 估算](#codex-hooks-與本機-token-估算)）：

   ```powershell
   companion\.venv\Scripts\python.exe companion\install_hook.py
   ```

5. **設定登入時自動執行：**

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1
   powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Status
   ```

6. **確認運作。** log 在 `%LOCALAPPDATA%\CodexUsageDisplay\companion.log`。Claude Code 回應一則訊息後，`%LOCALAPPDATA%\CodexUsageDisplay\claude-rate-limits.json` 會出現並持續更新，顯示器的 Claude 頁約 15 秒內會出現剩餘百分比。

全部移除：

```powershell
powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Uninstall
companion\.venv\Scripts\python.exe companion\install_claude.py --uninstall
companion\.venv\Scripts\python.exe companion\install_hook.py --uninstall
```

## 配對與 BLE 行為

`--once` 不連藍牙，只印出實際要送的 v3 JSON；韌體也接受舊版 v1/v2 格式。連線後會立即送出一次資料，之後每 15 秒送一次心跳，狀態有變化時也會立即送出。Codex app-server 的完整用量每 60 秒更新一次。掃描或重連失敗時，會以有上限的指數退避重試。Claude 資料走同一個服務、同一組配對裡的第二個特徵值（`CLAUDE_STATUS_UUID`，同樣是 v3 JSON）。

需要重新配對時，在顯示器的系統設定執行「清除配對」，必要時刪除 Windows 藍牙裡殘留的裝置，再重新配對。藍牙初始化失敗不會影響時鐘或 Wi-Fi 頁面。

掃描不到裝置時，確認 Windows 設定裡藍牙已開啟、刪除殘留的 `Codex Display` 裝置，並確認公司的隱私權政策允許桌面應用程式使用藍牙。Windows 不像 macOS，沒有個別終端機的藍牙權限提示。

## Codex hooks 與本機 token 估算

```powershell
python companion\install_hook.py
python .codex\hooks\codex_display_event.py --bootstrap
```

安裝程式可重複執行，會保留 `%USERPROFILE%\.codex\hooks.json` 裡其他無關的設定。`UserPromptSubmit` 與 `Stop` 只把工作階段、回合與路徑資訊附加到 `%LOCALAPPDATA%\CodexUsageDisplay\hook-events.jsonl`，不會複製提示或回應內容。`Stop` 會從對應的 rollout 逐步讀取 token 紀錄，並以原子寫入維護 `local-usage.json`。只有在官方 UTC 每日用量拿不到時，才會顯示加上 `~` 的本機估算值。佇列檔達 1 MiB 會輪替，保留一份封存。

移除 hooks：

```powershell
python companion\install_hook.py --uninstall
```

## Claude Code 整合

`install_claude.py` 會修改 `%USERPROFILE%\.claude\settings.json`（或 `%CLAUDE_CONFIG_DIR%` 指定的位置），修改前先寫一份帶時間戳的 `settings.json.bak-*` 備份，並保留其他無關設定。它會安裝三樣東西：

- **`statusLine`** → `claude_display_event.py --statusline`：在終端機使用 Claude Code 時記錄額度。原本若有 statusLine 會串接繼續執行，移除時原樣還原。
- **`rlcd-usage-bridge` mod**（`companion\rlcd-usage-bridge`，以本地 marketplace `rlcd-local` 註冊）：VS Code 擴充功能不會執行 `statusLine`，這個 mod 會依每個工作階段自己的額度量測，寫出同一個額度檔，在終端機、桌面版與 VS Code 都能運作。它使用 Claude Code 標示為早期存取（early access）的外掛 API，Claude Code 更新後可能需要調整。
- **`UserPromptSubmit` / `Stop` / `SessionEnd` hooks**：追蹤執行中的工作階段。

存放在 `%LOCALAPPDATA%\CodexUsageDisplay` 的檔案：

- `claude-rate-limits.json`：5 小時與 7 天的已用百分比與重置時間。
- `claude-sessions.json`：執行中回合的工作階段 ID 與時間戳，不含任何提示內容。

額度要等 Claude Code 工作階段收到第一次 API 回應後才會出現；在那之前，顯示器會把該視窗顯示為無資料。token 總量來自 `projects/**/*.jsonl` 對話紀錄：輸入、輸出、快取建立與快取讀取 token 全部加總，同一則訊息只算一次，「今日」與「7 天」都以本地日期計算。一個工作階段最多只會被算作執行中 6 小時。

## 登入時自動執行

`install_windows_task.ps1` 建立的是每位使用者各自的排程工作：以 `pythonw.exe` 在背景執行，登入時啟動，失敗會重試，使用電池時也會執行，log 寫在 `%LOCALAPPDATA%\CodexUsageDisplay`，達 1 MiB 會輪替。它只負責執行 companion；Claude 與 Codex 的整合要另外安裝（上面第 4 步）。

## 測試

```powershell
python -m unittest discover -s companion\tests -v
```

Codex app-server 介面、rollout 格式與 Claude Code 對話紀錄格式都是本機整合點，不是長期公開的 API，更新後可能需要調整解析程式。

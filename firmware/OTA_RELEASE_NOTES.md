v100.0.8：

## 重要提示

1. 本版本未變更分區表；已使用 v100.0.4 以後分區的裝置可直接 App-only OTA。v100.0.3 或更早版本的裝置仍須先完整刷寫對應語言的 `_merged.bin`。
2. Claude 頁的 Fable、付費與重設額度需要同時更新 Windows companion：更新原始碼後重新啟動 companion，並重開 Claude Code 或 VS Code。搭配舊 companion 時，這些項目顯示 `--`，5 小時與 7 天照常顯示。

## 新功能

1. Claude 頁改為列表版面：5 小時、7 天、Fable 週額度與付費額度各一列，進度條都代表剩餘量；付費列顯示剩餘預算與上限。
2. Claude 頁下方列出可用的重設額度，分種類並附到期日；Claude Code 未登入時顯示「未登入」。
3. Codex 頁改用相同的列表版面（5 小時、7 天、付費），下方顯示完整重設次數與最近到期日。
4. 在 Claude 或 Codex 頁長按 BOOT，可把下方區塊切換為使用統計（今日與 7 天 token 數、執行中工作數），兩頁分別切換。

## 變更

1. 在 Claude 或 Codex 頁長按 BOOT 不再切換時鐘的秒數顯示設定。

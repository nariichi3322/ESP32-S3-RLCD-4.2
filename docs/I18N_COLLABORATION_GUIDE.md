# i18n 協作指南 / i18n Collaboration Guide

本指南定義 ESP32-S3 RLCD 4.2 firmware 的 internationalization（i18n）協作規則。
所有新的 user-visible text、翻譯、字型、版面與 language-switching 修改，都應依照本文件處理。

## 1. 支援語言與 source of truth

目前支援三種 UI language：

| `UiLanguage` | 語言 | 預設 | 備註 |
| --- | --- | --- | --- |
| `Traditional` | Traditional Chinese（繁體中文） | 是 | 目前的 default language |
| `Simplified` | Simplified Chinese（简体中文） | 否 | 必須使用簡體字詞 |
| `English` | English | 否 | 必須優先考慮小螢幕 layout |

主要 source files：

- `main/ui/ui_language.h`：公開 language API 與 `UiLanguage`。
- `main/ui/ui_language.cpp`：目前 language state、legacy literal mapping 與 revision。
- `main/ui/ui_i18n.h/.cpp`：以 stable `UiTextId` 管理 weather、weekday、AQI、wind 等共用文字。
- `main/ui/ui_widgets.h/.cpp`：共用 LVGL label 建立、更新與 CJK font fallback。
- `main/assets/ui_fonts.h`：LVGL font declarations。
- `main/storage/device_settings_persistence.cpp`、`main/storage/saved_config_loader.cpp`：language 的 NVS persistence 與 startup restore。
- `main/ui/ui_views.cpp`：偵測 language revision 並重建一般 firmware UI。

新的翻譯不應以「某個頁面恰好使用的中文 spelling」作為唯一 identity。新共用文字優先使用
`UiTextId`；頁面專用或含參數的文字使用明確的三語 `ui_language_text()`。

## 2. API 選擇規則

### 2.1 Stable ID：固定、重複使用的語意文字

適合 weather description、weekday、AQI category、wind direction 等 lookup text：

```cpp
const char *text = ui_i18n_text(UiTextId::WeatherRain);
```

新增 `UiTextId` 時，必須同時在 `main/ui/ui_i18n.cpp` 的 `kTexts` 加入同位置的
Traditional、Simplified、English 三語列。檔案中的 `static_assert` 會檢查 enum 與 table 的數量；
不要只新增 enum 或只新增 table row。

### 2.2 三語 inline text：頁面專用或含 format placeholder 的文字

```cpp
const char *format = ui_language_text(
    "溫度 %s°C",
    "温度 %s°C",
    "Temp %s°C");
snprintf(buffer, sizeof(buffer), format, value);
```

三個版本必須保留相同的 placeholder 契約：

- `%s`、`%d`、`%02d` 等 placeholder 的數量與 type 必須一致。
- 若語序不同，允許改變 placeholder 順序，但必須同步調整 formatting code。
- 先選擇 format，再執行 `snprintf`；不要先生成完整中文句子後才呼叫 localization。
- buffer size 必須以三種語言中最長的實際輸出估算，包含數字、單位與終止 `NUL`。

新 code 不應省略第三個 `english` 參數。兩參數形式只為既有 compatibility call 保留，
其英文結果依賴 `ui_language.cpp` 的 `legacy_english()`，容易遺漏或產生不一致命名。

### 2.3 `ui_language_localize()`：legacy compatibility only

`ui_language_localize()` 只會處理 `kLocalizedLiterals` 中的 exact Traditional/Simplified literal，
並在 English 模式透過 `legacy_english()` 尋找舊 mapping。它不是一般用途的 translator：

- 不會翻譯 arbitrary API text、city name、SSID 或使用者輸入。
- dynamic formatted text 通常不會 exact match，因此不能依賴它翻譯 format output。
- 新增 user-visible text 時，請直接使用 `UiTextId` 或三語 `ui_language_text()`。
- 修改既有 legacy literal 時，必須同步檢查 `kLocalizedLiterals` 與 `legacy_english()`，並補 test。

## 3. LVGL label 與 font 規則

### 3.1 一般 label

一般 user-visible label 應使用共用 helper：

```cpp
lv_obj_t *label = make_label(screen, x, y, width, height, text);
set_label_text_if_changed(label, text);
```

`make_label()` 預設使用 `zh_font_16`。`make_label_with_font()` 與
`set_label_text_if_changed()` 會檢查 text 是否含 CJK；當指定 font 沒有 fallback 時，會改用
`zh_font_16`，避免 Montserrat 顯示方框字。

English text 若不含 CJK，shared `zh_font_16` 會在適用時降為 Montserrat 14，以保留固定寬度
panel 的可讀邊界。

### 3.2 Font 使用原則

- `zh_font_16` 覆蓋 Traditional/Simplified Chinese、常用 punctuation、ASCII，並 fallback 到
  `lv_font_montserrat_14`。
- `zh_flip_lunar_22`、`zh_pomodoro_title_24` 只適合其指定的大字標題或特殊元件；不要拿來當
  一般小欄位的通用 font。
- `weather_icons_36` 是 icon font，只能用於 icon label，不可承載中文或一般英文句子。
- 不要對可能包含中文的 label 直接固定成沒有 fallback 的 Montserrat font。
- 若必須直接呼叫 LVGL API 更新 text，先確認該 label 的 font 能涵蓋三語；一般情況應改用
  `set_label_text_if_changed()`。
- `font_for_label_text()` 是 safety net，不是 layout 設計替代品；新頁面仍須選擇適合欄位大小的
  font、width、height 與 long mode。

## 4. Layout 與英文翻譯規則

ESP32 裝置是 400 × 300 RLCD，許多頁面使用固定寬度欄位。英文通常比中文長，翻譯完成不等於
畫面一定能容納。

新增或修改英文時：

1. 先確認 label 的 pixel width、height、font size、alignment 與 LVGL long mode。
2. 對窄欄位使用清楚且短的英文，例如 `Temp/Humi`、`Net test`、`P.cldy`；不要只把文字設成
   clip 後宣稱完成。
3. 對說明句使用 wrap 或拆成多行；不要用極小字體掩蓋過長翻譯。
4. 不要為了英文縮短 Traditional/Simplified Chinese；三種語言應各自保持自然且正確。
5. 注意英文大小寫、punctuation、unit、百分比與 degree symbol 的寬度。
6. 對日期、weekday、weather description、AQI、wind direction、sunrise/sunset 等動態文字，
   測試 placeholder、正常值與邊界值。

外部 API 或 lookup table 的文字必須先分類：

- WMO weather code 應透過 `ui_weather_text()` / `ui_i18n_text()`。
- weekday 應透過 `ui_weekday_text()`，不可在各頁面自行建立星期表。
- AQI 與 wind direction 應使用 `ui_air_quality_category()`、`ui_wind_direction()`。
- city name、SSID、user input 與 arbitrary remote saying 是資料，不可當 translation key 強制替換。
  若產品要求翻譯該資料，應在資料來源或專用 lookup layer 定義明確規則。

## 5. Runtime language switching 與 persistence

Language 設定的正規流程是：

```text
settings action
    -> set_ui_language_setting()
    -> save language enum to NVS
    -> ui_language_store()
    -> increment ui_language_revision()
    -> ui_views detects revision
    -> rebuild affected firmware UI
```

協作時請遵守：

- UI page 不要直接呼叫 `ui_language_store()`；使用 storage layer 的設定 API。
- 不要在 static/global cache 長期保存已選出的 translated pointer，因為 language 改變後該 pointer
  仍可能指向舊語言。
- page rebuild 之外的長生命週期 widget，必須在 language revision 或對應 refresh event 時重新
  執行 `set_label_text_if_changed()`。
- 新增 page、overlay、modal、status panel 或 cached label 時，明確決定它在 language change
  時是 rebuild、refresh，還是刻意不受影響，並在 code comment/PR 說明。
- setup portal 是獨立的 runtime surface；其 HTML response 應在產生 response 時取得目前語言，
  不可把前一次 response 的文字當永久 cache。
- startup 必須透過 saved config load normalize NVS value，再 store runtime state。未知的 stored
  value 應回到 Traditional default，不可造成 invalid enum。

## 6. Translation table 與命名規則

### `UiTextId`

- enum 使用 PascalCase，例如 `WeatherPartlyCloudy`、`WeekdaySunday`。
- ID 表達語意，不表達頁面座標、font 或目前中文 spelling。
- 同一語意只建立一個 ID，避免各頁面各自複製 weather/weekday/AQI 翻譯。
- `Count` 必須維持在 enum 最後；table 需維持完全相同順序。

### Page-local text

- 常數依現有模組慣例命名，例如 `kWeatherBoard...`、`kPortal...`。
- 若拆出三語常數，使用 `Traditional`、`Simplified`、`English` 後綴，並在同一個 semantic group
  放置，避免三種語言散落在不同檔案。
- 同一個 UI concept 的命名要一致：例如 `Weather city`、`Network diagnostics`、`Pairing`。
- 不要把 log-only text、protocol value、HTTP URI、JSON key 或 enum name 放進 UI translation table。

### Font/layout naming

使用能表達用途的 font/resource 名稱；icon font 與 text font 不要混用。固定尺寸應放在相關
`*_layout.h`，並在修改翻譯時一起檢查，不要在 translation code 內偷偷改座標。

## 7. 新增語言（Language Extension）

新增一種語言與新增一個 translation string 是不同層級的工作。目前實作不是 plug-in 式語言
架構，以下項目都固定假設 Traditional、Simplified、English 三種語言：

- `UiLanguage` enum 的既有 numeric values。
- `ui_language_text(traditional, simplified, english)` 的三個參數。
- `ui_i18n.cpp` 的 `TextTriplet` 與 `kTexts` table。
- `ui_language_localize()` 的 Traditional/Simplified literal pair 與 English legacy mapping。
- Settings language selector、font resource、host tests 與 UI verification。

因此，不能只在 `kTexts` 增加一欄，也不能只修改 `legacy_english()` 就宣稱完成新增語言。

### 7.1 先決定 extension design

開始修改前，先建立明確的 design decision：

1. 保留 typed `UiTextId`，將固定三欄的 text provider 改為依 `UiLanguage` index 查表；或
   引入其他等價的集中式 resource provider。
2. 決定 translation missing 時的 fallback policy。新語言正式 exposed 前，最好要求所有
   user-visible text 都有完整翻譯；若允許 fallback，必須明確指定 fallback language，且不能
   靜默退回任意 source literal。
3. 決定 API 如何處理 page-local text 與 format string，避免新增第四、第五個 positional argument
   讓 call site 越來越脆弱。
4. 評估 table、font 與 runtime state 對 Flash、RAM、binary size、partition space 的影響。不要
   因為方便而把整套大型 font 無限制加入 firmware。

這個 design decision 應在 PR 中記錄，並說明是否維持既有三語 call site 的 source compatibility。

### 7.2 實作與 backward compatibility checklist

新增語言時，依下列順序處理：

1. 為既有 `Traditional = 0`、`Simplified = 1`、`English = 2` 保留 NVS numeric values；不可
   重新排列 enum，避免既有裝置在 reboot 後變成另一種語言。新 value 應明確指定並加入
   `normalize_ui_language()`。
2. 更新 `ui_language_load()`、`ui_language_store()`、language predicates、revision behavior，
   確認未知或損壞的 stored value 仍有 deterministic fallback。
3. 更新 Settings language selector、顯示名稱、儲存流程與 language-change feedback。
4. 將共用 translation resource、page-local text、format string、weather/weekday/AQI/wind
   lookup 全部遷移到新的 provider；不可遺留只支援舊三語的 hidden path。
5. 檢查 `ui_language_localize()` 的 legacy table。它只能作 migration compatibility；若新語言
   需要依賴 exact literal mapping，應重新設計資料結構，不要繼續堆疊特殊 case。
6. 為新語言建立完整 font coverage。除了 glyph，也要確認 punctuation、unit、degree symbol、
   fallback chain 與 generated font 的 Flash 佔用。
7. 重新檢查所有固定寬度 label、wrap/clip mode、buffer size、日期格式與動態資料。新語言較長
   或使用不同 script 時，應優先調整 layout/resource policy，而不是全域縮小字體。
8. 確認 language revision 會刷新所有 firmware UI、overlay、status、modal、cached label 與
   portal response；不能只刷新 Settings 頁。

### 7.3 驗證要求

新增語言至少需要：

- enum normalization 與既有 NVS values 的 regression test。
- translation provider 的完整 key/row/count 檢查。
- 每個 lookup helper 的正常值、未知值與邊界值測試。
- format placeholder、buffer size 與 fallback behavior 測試。
- Settings 切換、runtime refresh、reboot restore 測試。
- 所有工作頁、Codex/Xiaozhi、Settings、OTA/status、portal 的畫面檢查。
- 新字型的 glyph coverage 與 firmware size 檢查。

若沒有實機或可代表該語言 font/layout 的 simulator verification，PR 必須明確標示為未驗證，
不可只以 host test 通過作為完成證據。

## 8. 新增或修改文字的工作流程

1. 用 `rg` 搜尋呼叫點與所有現有相同文字，確認它是 user-visible、log、protocol 還是資料。
2. 決定使用 stable ID、三語 inline text 或 legacy compatibility；新 code 優先前兩者。
3. 同時撰寫 Traditional、Simplified、English；確認三者語意、placeholder 與 units 一致。
4. 檢查所有使用該 text 的 label font 與 LVGL long mode。
5. 檢查動態更新、language revision rebuild、startup restore 與 cache 是否會留下舊語言。
6. 為 lookup/format helper 增加 host test；至少覆蓋三種語言、正常值、未知值與邊界值。
7. 執行 targeted compile/static checks；UI 修改應提供三語畫面或硬體驗證證據。
8. PR description 記錄修改檔案 ownership、layout 風險、verification 結果與尚未驗證項目。

## 9. 建議的檢查指令

以下指令可協助找出漏翻譯、繞過 helper 或未更新的直接 LVGL call：

```bash
rg -n 'lv_label_set_text|lv_label_set_text_fmt|lv_label_set_text_static|make_label|set_label_text_if_changed' main/ui main/network
rg -n 'ui_language_text|ui_language_localize|ui_i18n_text|ui_weather_text|ui_weekday_text' main tests
git diff --check
```

若已有 configured ESP-IDF build directory，優先編譯受影響 object 或 component target；不要為了
一般文字修改預設執行完整 project build。至少應執行相關 host tests，例如：

- `tests/ui_language_host_test.cpp`
- `tests/ui_i18n_host_test.cpp`
- `tests/ui_weather_board_text_host_test.cpp`
- `tests/ui_settings_content_host_test.cpp`
- `tests/ui_work_page_catalog_host_test.cpp`

實機或 simulator review 應逐一確認：

- Traditional、Simplified、English 三種 language。
- Settings、weather clock、weather board、flip clock、calendar、history、gallery、Xiaozhi、Codex。
- 初始 placeholder、正常資料、network error/offline、OTA/status、動態 API data。
- 切換語言後不離開目前流程的畫面，以及 reboot 後的 language restore。
- 中文沒有方框字；英文沒有明顯 clipping、重疊或過度縮小。

## 10. Review checklist

- [ ] 所有新的 user-visible text 都有 Traditional、Simplified、English。
- [ ] 新文字沒有誤用 `ui_language_localize()` 或兩參數 compatibility API。
- [ ] `UiTextId` 與 `kTexts` row 數量、順序一致。
- [ ] format placeholder、buffer size、units 與 punctuation 已檢查。
- [ ] 所有 CJK label 使用具備 glyph/fallback 的 font。
- [ ] English fixed-width label 已檢查 pixel fit、wrap/clip mode 與 alignment。
- [ ] dynamic/cached labels 在 language change 後會更新。
- [ ] API/lookup 文字已與 UI translation key 分離。
- [ ] language persistence、startup restore 與 unknown enum normalization 已檢查。
- [ ] targeted compile、host test、`git diff --check` 結果已記錄。
- [ ] 尚未進行的實機或視覺驗證已明確列出。

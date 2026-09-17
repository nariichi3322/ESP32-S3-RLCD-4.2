# WeatherClock 固件独立工程 / Standalone Firmware

本目录可单独下载并编译，无需 `host_web/`、仓库根目录脚本或其他示例工程。
固件源码、板级组件、中文字体、内置图片/音频、分区表及依赖锁均保存在本目录。

安装并激活 ESP-IDF **v5.5.5**，进入本目录执行：

```sh
idf.py build
```

首次构建需联网，由 ESP-IDF 组件管理器按 `dependencies.lock` 下载 LVGL、ESP-SR 等依赖到 `managed_components/`；语音模型由构建过程生成。构建产物在 `build/`，不需要从其他项目目录复制文件。

完整串口烧录并查看日志：

```sh
idf.py -p YOUR_SERIAL_PORT flash monitor
```

`flash` 会写入分区表、应用、OTA 初始化数据和语音模型；需要保留设备原数据时先核对现有分区布局，不执行 `erase-flash`。

BLE modem-sleep A/B 测试：正式默认配置保持 `CONFIG_BT_CTRL_MODEM_SLEEP=n`。要建立 B 版本，请在 PowerShell 中使用独立的 build 目录与 sdkconfig：

```powershell
$env:SDKCONFIG_DEFAULTS = "$(Get-Location)\sdkconfig.defaults;$(Get-Location)\sdkconfig.modem_sleep_b.defaults"
idf.py -B build_modem_sleep_b -D SDKCONFIG=build_modem_sleep_b/sdkconfig reconfigure
idf.py -B build_modem_sleep_b build
Remove-Item Env:SDKCONFIG_DEFAULTS
```

该文件不会被正式默认 build 自动套用。比较 USB 列举、BLE 稳定性、待机电流及 CODEX 60 秒宽限行为后，才决定是否采用。

公开源码默认使用占位 OTA 地址。自建固件需要在 `main/core/ota_endpoint_local.h` 配置自己的 `WEATHER_CLOCK_OTA_MANIFEST_URL` 和 `WEATHER_CLOCK_OTA_BACKUP_MANIFEST_URL`；该本地配置不能提交。缺少此文件不影响编译。正式发行固件的 OTA 地址由 GitHub Actions 注入。

## 版本与正式 OTA

当前源码准备发布版本为 **v100.0.4**，版本值定义在 `CMakeLists.txt` 的 `PROJECT_VER`，并必须与 GitHub Release tag 完全一致。正式 Release 会分别构建 `zh-TW`、`zh-CN`、`en` 和 `ja` 四种语言的 App 镜像，以及对应的完整刷写镜像；`firmware/` 中的 OTA 清单由构建流程在取得实际固件后自动写入 SHA-256 与文件大小。

v100.0.4 调整了分区表：新增 `lang` 分区并缩小 `assets` 分区。使用 v100.0.3 或更早版本分区表的设备，必须先以对应语言的 `_merged.bin` 完整刷写，不能直接使用 App-only OTA；完成分区迁移后，后续相同分区格式的版本才可使用 App-only OTA。完整刷写前请先备份需要保留的 NVS 设置与自定义资源。

本地构建可通过 `APP_UI_LOCALE=zh-TW`、`zh-CN`、`en` 或 `ja` 选择语言，例如：

```sh
idf.py -B build_en -DAPP_UI_LOCALE=en build
```

`APP_UI_LOCALE` 只决定该 App 镜像的编译语言；Release OTA 清单会根据设备目标语言选择对应映像。

## Version and official OTA

The source tree is being prepared as **v100.0.4**. The version is defined by `PROJECT_VER` in `CMakeLists.txt` and must exactly match the GitHub Release tag. Official Releases build `zh-TW`, `zh-CN`, `en`, and `ja` App images plus matching full-flash images; the OTA manifests under `firmware/` receive their SHA-256 values and file sizes from the build workflow after the actual firmware exists.

v100.0.4 changes the partition table by adding `lang` and shrinking `assets`. Devices using the v100.0.3 or earlier layout must first flash the matching `_merged.bin` and must not use App-only OTA directly. After the partition migration, App-only OTA is available only to devices confirmed to use the new layout. Back up NVS settings and custom resources before a full flash.

Select the compiled locale with `APP_UI_LOCALE=zh-TW`, `zh-CN`, `en`, or `ja`, for example:

```sh
idf.py -B build_en -DAPP_UI_LOCALE=en build
```

`APP_UI_LOCALE` selects the compiled locale of that App image. The Release OTA manifest selects the matching image for the device's target locale.

## English

This directory is a standalone ESP-IDF **v5.5.5** project. Activate that SDK, enter this directory and run `idf.py build`. No sibling directory or repository-root script is required.

Firmware sources, board components, fonts, bundled media, partition configuration and the dependency lock are included here. The first build requires network access to download locked managed components; speech models are generated during the build. Output is written to `build/`.

Public source uses placeholder OTA endpoints unless a local `main/core/ota_endpoint_local.h` defines the two OTA URL macros. This optional configuration is not needed for compilation and must not be committed. Official Release builds inject their endpoints in GitHub Actions.

For the BLE modem-sleep A/B test, the formal default keeps `CONFIG_BT_CTRL_MODEM_SLEEP=n`. Build variant B only in a separate build directory by setting `SDKCONFIG_DEFAULTS` to include `sdkconfig.modem_sleep_b.defaults` and passing a separate `SDKCONFIG` cache entry before running `idf.py build`. The variant enables modem sleep Mode 1 with the Main XTAL and is not applied by the formal default build. Compare USB enumeration, BLE stability, standby current, and the CODEX 60-second grace behavior before considering any default change.

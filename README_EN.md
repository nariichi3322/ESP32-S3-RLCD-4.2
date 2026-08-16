# ESP32-S3 RLCD 4.2 Weather Clock

> **Language:** English (current) · [Chinese](README.md)

This is a low-power weather clock firmware project built around the **ESP32-S3** and a **4.2-inch RLCD display**. It combines an always-readable clock, local temperature and humidity sensing, online weather data, a calendar, picture display, audio reminders, Xiaozhi AI, and OTA updates in one desktop device.

## Quick Links

- [Chinese User Guide](docs/User_zh.md)
- [English User Guide](docs/User.md)
- [Contributing Guide](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- [Project License](LICENSE)
- [Minimum-power Power Demo](docs/Power%20Demo/README.md)

## Related Projects

- [Official Waveshare ESP32-S3-RLCD-4.2 product page](https://www.waveshare.com/product/esp32-s3-rlcd-4.2.htm): official board overview, specifications, and purchasing information.
- [Official Waveshare ESP32-S3-RLCD-4.2 documentation](https://docs.waveshare.com/ESP32-S3-RLCD-4.2): interfaces, schematic, examples, and hardware resources.
- [ESP32-S3-RLCD-4.2_UP](https://github.com/wickenzh/ESP32-S3-RLCD-4.2_UP): OTA firmware mirror with available firmware and version information.
- [ESP32-S3-RLCD-4.2_Web](https://github.com/wickenzh/ESP32-S3-RLCD-4.2_Web): web-based desktop client for device configuration and custom resource management.

## Project Scope

The project targets a desktop weather clock intended to remain powered and readable for long periods. The design does not keep every subsystem active continuously. Wi-Fi, audio, high-frequency UI updates, and other expensive resources run only when the current feature needs them.

Core goals:

- **Always-readable display:** the RLCD retains visible information without continuous repainting.
- **Demand-driven power use:** page-specific updates stop when a page is hidden, and normal network sessions shut Wi-Fi down when finished.
- **Offline operation:** the RTC maintains time, and the device can enter offline mode without network configuration.
- **Stability first:** OTA, audio, display, and network tasks use explicit state and resource guards.
- **Maintainable structure:** pages, weather, configuration, audio, sensors, OTA, and Xiaozhi AI are maintained by responsibility.

## Seven Work Pages

The default order is shown below. Pages can be disabled or reordered in Settings, while the firmware always keeps at least one work page enabled.

1. **Weather Clock:** time, date, current weather, alerts, local temperature and humidity, battery, and status icons.
2. **Picture Clock:** local images, a large minute clock, and a daily saying; custom galleries support selectable rotation intervals.
3. **Weather Board:** city weather, air quality, humidity, wind, sunrise and sunset, alerts, and multi-day forecasts.
4. **Temperature/Humidity Clock:** high-contrast hours, minutes and seconds, local sensor data, trends, date, and lunar date.
5. **Calendar:** current-month calendar, lunar dates, festivals, and today highlighting; six-row months keep the current date visible.
6. **Temperature/Humidity History:** local temperature and humidity history with trend information.
7. **Xiaozhi AI:** local wake word, voice conversations, captions, spoken replies, one-shot alarms, Pomodoro timers, and weather-city configuration.

Every page follows a partial-refresh-first policy. Second-level pages update only changing digits, minute-level pages redraw only when time, data, or visible state changes, and hidden pages do not continue unnecessary rendering.

## Provisioning and Networking

Initial configuration is completed through the device setup portal:

- A primary Wi-Fi network and an optional backup network are supported. After repeated primary-network failures, the device can try the backup and promote it according to the existing failover policy.
- Online weather requires both a QWeather API Key and the account-specific API Host shown in the QWeather Console.
- Weather location can use public-IP geolocation or a manually selected city configured through the setup portal, desktop client, or Xiaozhi AI.
- Without Wi-Fi, the user can enter a local date and time and start the device in offline mode.

Network activity is demand-driven:

- Weather Clock and Weather Board request weather only when the page is enabled and data is missing or due for synchronization.
- Picture Clock requests the daily saying only when needed.
- NTP runs during startup, manual synchronization, and scheduled time-maintenance points.
- OTA, Network Diagnostics, and manual synchronization run only after explicit user actions.
- Xiaozhi AI activates its voice and network session only on its page, then releases session resources when the page is left.
- Offline mode, low battery, provisioning, and OTA states block unsuitable background network work.

Weather and daily-saying data are cached. A temporary network failure does not erase existing content, and bounded retries with backoff prevent repeated Wi-Fi power cycling.

## Reminders and Interaction

- Two physical buttons handle page switching, menu movement, confirmation, return, and stopping alert audio.
- Hourly reminders, all-day reminders, and multiple volume levels are supported.
- Xiaozhi AI can configure one one-shot alarm and can start, replace, query, or cancel a Pomodoro timer.
- Alarm and Pomodoro state remain independent, with conflict protection for identical trigger times.
- The Xiaozhi energy-saving option returns to the first page after an idle period; an active Pomodoro timer suspends that automatic return.

## Low-power Design

The firmware reduces unnecessary work while keeping the display visible:

- Second-level pages use partial updates; static pages suspend periodic rendering when no event is pending.
- Temperature, humidity, battery, and history sampling are scheduled according to time of day and charging state.
- Weather, daily saying, NTP, and OTA work is staggered to avoid startup memory and Wi-Fi peaks.
- Audio codec resources are released after playback.
- The Wi-Fi icon follows the real radio state so persistent high-power networking remains visible.
- OTA keeps percentage, speed, and progress feedback while reducing display-update pressure during downloads.
- Low-battery mode switches to a minimal display and blocks expensive operations.

The standalone [Power Demo](docs/Power%20Demo/README.md) under `docs/Power Demo/` explores the minimum-power boundary with retained display output, minute-level wakeups, and minimal peripherals. It does not include the full settings, OTA, multi-page, or Xiaozhi feature set and is not part of the production firmware build.

## Hardware and Software

- Board: [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/product/esp32-s3-rlcd-4.2.htm).
- MCU module: ESP32-S3-WROOM-1-N16R8 with 16MB Flash and 8MB PSRAM.
- Display: 4.2-inch 400 × 300 RLCD.
- Local devices: RTC, temperature/humidity sensor, battery ADC, and physical buttons.
- Audio: microphone, codec, power amplifier, and speaker for Xiaozhi AI and alert sounds.
- Network: Wi-Fi for provisioning, weather, daily saying, NTP, OTA, and diagnostics.
- Storage: NVS for network and user settings, plus a separate resource partition for replaceable assets.
- Framework: ESP-IDF `v5.5.3`; the UI is based on LVGL.

## Source Layout

- `main/`: application entry point and firmware business modules.
- `components/`: board support, display, audio, resources, and third-party components.
- `partitions.csv`: ESP32-S3 partition table.
- `CMakeLists.txt`: ESP-IDF project entry point.
- `sdkconfig.defaults`: default project configuration.
- `docs/`: user guides and the standalone Power Demo.
- `.github/`: public firmware build and source-release workflow.

See the [Contributing Guide](CONTRIBUTING.md) for build and contribution requirements. See the [Chinese User Guide](docs/User_zh.md) or [English User Guide](docs/User.md) for full flashing, provisioning, page operation, and troubleshooting instructions.

## OTA and Custom Resources

The device reads an OTA manifest and downloads an App firmware image. When a public version tag is published, the source repository builds two release artifacts:

- `weather_clock_vX.X.X.bin`: App-only image for OTA or address-specific App flashing while preserving the existing partition table and NVS.
- `weather_clock_vX.X.X_merged.bin`: complete image containing the bootloader, partition table, OTA data, speech models, and App for recovery or partition-layout changes.

OTA cannot update the partition table. When release notes require a full flash, do not use App-only OTA as a substitute. The desktop client can also write custom image resources; built-in assets remain available as a fallback when custom data is absent or invalid.

## Open-source Origin and Third-Party Licenses

The Xiaozhi AI page is ported and adapted from [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32), using commit [`7b190b78e4f8dfef14126f6cd478c134b3cd3cd8`](https://github.com/78/xiaozhi-esp32/commit/7b190b78e4f8dfef14126f6cd478c134b3cd3cd8) as the fixed reference baseline.

`xiaozhi-esp32` is published by Shenzhen Xinzhi Future Technology Co., Ltd. and project contributors under the MIT License. Its copyright notice, complete MIT terms, and disclaimer are preserved in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). The project's non-commercial license applies only to original work owned by this project's maintainer and does not override rights granted by third-party licenses.

## Contributing and Security

Before opening an issue or pull request, read:

- [Contributing Guide](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)

Do not publish Wi-Fi passwords, QWeather API keys, private API Host configuration, tokens, NVS images, device private keys, absolute local paths, or private service endpoints in issues, logs, screenshots, or commits.

## Usage Restrictions

This project is provided only for personal study, research, evaluation, and other non-commercial purposes.

**Commercial use is strictly prohibited.**

Without prior written permission from the project owner, maintainer-owned original work may not be used in commercial products, paid services, bulk sales, resale, for-profit distribution, or any other commercial delivery. Third-party components remain governed by their original licenses. See [`LICENSE`](LICENSE) for the full terms.

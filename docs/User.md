# WeatherClock Quick Guide

[Detailed guide](User_Detailed.md) · [Simplified Chinese](User_zh.md) · [Traditional Chinese](User_zh_TW.md) · [Japanese](User_ja.md)

## Set up the device

1. Power on and join the WeatherClock hotspot shown on the display. Use its displayed password.
2. Open the setup portal, or visit `http://192.168.4.1/`. Stay connected if your phone reports no internet.
3. Enter your primary Wi-Fi details. Backup Wi-Fi and a manual weather city are optional; no weather API key is needed.
4. Save and wait for validation. Correct any reported errors; successful setup opens a work page.

For offline use, leave Wi-Fi blank and enter a date and time. Offline date/time fields can stay empty for online setup.

## Use the buttons

- Short **BOOT**: next work page; confirm within settings.
- Short **KEY**: open settings; move the selection.
- Hold **KEY**: back one level or leave settings.
- While an alarm or focus-completion sound plays, either button stops it.

Settings close after about 30 idle seconds. The firmware UI is primarily in Simplified Chinese; this guide does not imply a language selector.

## Everyday use

Eight pages provide Weather Clock, Picture Clock, Weather Board, Temperature & Humidity Clock, Calendar, History, Xiaozhi AI, and Aggregate Clock. Display settings control page visibility and order. The first page is home, and at least one non-AI page must remain.

Weather refreshes as needed at hour boundaries. Built-in images follow the weekday; uploaded galleries have configurable rotation. Local readings update about every minute by day and every two minutes at night. AI consumes more power and can warm the board, affecting temperature readings.

AI tools can set a one-shot alarm, focus timer, and weather city. Check the on-screen result. Ordinary delayed reminders use the alarm; explicitly request focus or a Pomodoro timer for focused work. Replacing an existing alarm requires confirmation.

## Updates, assets, and help

- In System settings, check for updates with BOOT, then confirm an available update within 60 seconds.
- Use [WeatherClock Studio](https://nariichi3322.github.io/ESP32-S3-RLCD-4.2/) on a desktop browser for assets, previews, flashing, and logs.
- The web UI supports narrow-screen viewing and stacks the simulator vertically. Asset writing, firmware flashing and serial operations still require desktop Chrome/Edge with Web Serial.
- The Firmware page defaults to Online full installation. For a first install or partition migration, connect the device and verify its chip and Flash capacity before confirming the overwrite of firmware, partitions and assets. Use Advanced firmware flashing with an App update to preserve device settings or custom assets. Phones can browse but cannot perform serial installation.
- The top-right language selector supports English, Japanese, Simplified Chinese and Traditional Chinese (Taiwan). Your choice is remembered; firmware screens and the Wi-Fi setup demo keep their original language.
- Missing weather: check the network and the weather city. Cached values are not proof of a successful refresh.
- The Wi-Fi icon shows radio activity. Battery and charging indicators are estimates; USB connection does not mean continuous charging.
- Offline mode allows local pages only. Returning online requires complete credentials.
- **Never flash an App bin at 0x0. OTA does not change the partition table.** Read the detailed guide and release notes before a first installation or layout migration.
- Factory reset clears network and ordinary settings but preserves AI binding, history, and custom assets. It is not a whole-flash erase.

Keep credentials and private addresses out of public logs, screenshots, and quick-configuration links. See the [detailed guide](User_Detailed.md) for complete instructions.
The firmware currently uses UTC+8 with no timezone selector. A translated guide does not change the device timezone.

## Online and offline operation

Settings mode offers two independent paths. For online use, enter a primary Wi-Fi network and optionally a backup network, NTP server, and weather city. Saving a valid online configuration exits offline mode and schedules time, weather, and Daily Saying synchronization.

For offline use, enter the current local date and time. The device stores offline mode, stops Wi-Fi, blocks background network work, and hides network-dependent pages. Offline mode can be disabled directly when a valid Wi-Fi configuration is already stored; otherwise the device opens Settings mode.

## Weather

Weather data is supplied by Open-Meteo. No API key or custom host is required. A manually entered city is resolved with the Open-Meteo Geocoding API. If the city field is empty, the device obtains coordinates from public-IP location and sends those coordinates directly to Open-Meteo.

The Weather Board displays current conditions, a six-day forecast, and current air quality. Weather alerts are not displayed because they are not part of this integration. Weather codes follow the WMO interpretation table and use provider-neutral monochrome icons.

Weather data: [Open-Meteo Forecast API](https://open-meteo.com/en/docs) and [Geocoding API](https://open-meteo.com/en/docs/geocoding-api). Air-quality data includes CAMS-derived information through the [Open-Meteo Air Quality API](https://open-meteo.com/en/docs/air-quality-api). Please retain these attributions when redistributing the firmware or screenshots containing weather data.

## CODEX Usage Bluetooth

The CODEX page is controlled by the common page visibility and order settings. Bluetooth starts while the normal CODEX page is visible, and opening the Settings overlay from CODEX keeps the Bluetooth transport and pairing state available. Leaving CODEX for an ordinary work page requests a graceful stop: Bluetooth and its pairing/connection state are retained for up to 60 seconds, and returning to CODEX during that window cancels the stop. While the transport remains active, the top Bluetooth status icon continues to reflect its current link state on the visible work page. At the deadline, the transport stops and the icon disappears. Low-battery mode, OTA Checking or Updating, a queued or active setup portal, and non-Settings auxiliary pages stop Bluetooth immediately.

Use **System > Clear CODEX pairing** to remove saved bonds when pairing a different client.

## System menu

The System menu contains Offline Mode, Factory Reset, About, Clear CODEX Pairing, Language, Settings Mode, OTA, and Network Diagnostics. Network Diagnostics checks the Open-Meteo public endpoints together with Wi-Fi, DNS, NTP, Daily Saying, internet access, and the OTA source.

## Upgrading

On first startup after this migration, obsolete weather credential and legacy CODEX feature-switch values are removed from NVS. Wi-Fi networks, weather city, page visibility/order, language, alarm, and other settings remain intact; a factory reset is not required.

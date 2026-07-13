# WeatherClock User Guide

This guide explains first-time setup, the seven work pages, hardware keys, online/offline operation, reminders, Xiaozhi AI, OTA updates, DIY assets, and troubleshooting.

> **Important upgrade notice:** `v1.5.x` uses a new flash partition table. A device still running `v1.4.59` or earlier must be fully flashed with the merged image or complete flash package before using `v1.5.x`. An App-only flash or ordinary OTA cannot update the partition table.

## 1. Quick Start

1. Power on the device and wait for the startup animation to finish.
2. On first use, connect a phone or computer to the AP whose name starts with `WeatherClock-`.
3. The captive portal normally opens automatically. If it does not, browse to `http://192.168.4.1/`.
4. Choose an operating mode:
   - Online: enter Wi-Fi SSID, password, and QWeather API Key.
   - Offline: leave Wi-Fi empty and enter the offline date and time.
5. After setup, the clock opens the first enabled page in the saved page order.

Hardware key behavior:

- **BOOT short press:** move to the next enabled work page; confirm in Settings.
- **KEY short press:** open Settings; move the selection inside Settings.
- **KEY long press:** return from a secondary menu; exit Settings from the primary menu.
- **Either key during an alarm or Pomodoro completion sound:** stop the remaining sound and consume that key press.

Settings returns to the current work page after about 30 seconds without activity. Valid operations in the page-order screen restart this timeout.

## 2. Status Bar and Day Progress

Depending on the page, the status area shows date, weekday, battery, Wi-Fi, reminder, alarm, and local sensor information.

- **Wi-Fi icon:** shown only while Wi-Fi is actually connected.
- **Sound icon:** shown when an hourly or all-day reminder is enabled.
- **Alarm icon:** shown while the one-shot alarm is enabled.
- **Battery icon:** shows estimated charge. During detected charging it blinks on whole-second boundaries. The hardware has no dedicated CHG/VBUS input, so plug/unplug detection based on ADC voltage trends can be delayed briefly.
- **Day-progress strip:** shared by all seven pages. Its 60 segments each represent about 24 minutes and refresh only on page entry or when crossing a new segment.

The display favors partial refreshes. Second-level pages redraw only changing digits or small regions; low-frequency pages update only when minute, date, sensor, or network data changes.

## 3. Seven Work Pages

Press **BOOT** to follow the saved page order. Disabled pages are skipped. Every page can be disabled, but the device prevents disabling the final enabled page.

### 3.1 Weather Clock

Shows large time, date, battery, current weather, alerts, local temperature/humidity, trend arrows, status icons, and an animation area.

- Time and required animation regions use second-level partial refresh.
- Entering the page requests weather only when required data is missing or stale.
- Offline, low-battery, setup, and OTA states block ordinary weather requests.

### 3.2 Picture Clock

Shows a local image, large time, daily text, and local temperature/humidity summary.

- Built-in images are selected by weekday and change once per day.
- When a desktop-client gallery is installed, custom images take priority and rotate daily by date.
- Daily text is fetched only when the Picture Clock needs it and is not repeatedly downloaded after a successful update for the same day.

### 3.3 Weather Board

Shows city, current temperature and condition, air quality, humidity, wind, sunrise/sunset, time until the next sunrise or sunset, weather alerts, and a six-day forecast.

- Missing or stale data causes a full weather refresh when entering the page.
- Sunrise/sunset countdown updates once per minute.
- Local sensor and status text refresh only when their values change.

### 3.4 Temperature/Humidity Clock

Shows three high-contrast hour/minute/second cards plus local temperature, humidity, trend arrows, comfort faces, date, and lunar date.

- Seconds refresh locally once per second.
- Local temperature/humidity is sampled every minute during the day and every two minutes at night.
- Trend arrows use the rolling average of valid samples from the latest four-hour window and restart after reboot.

### 3.5 Calendar

Shows the current month, weekday bar, lunar text, and holidays.

- The calendar body redraws only on page entry, date/month change, or a time correction that crosses a day boundary.
- For rare six-row months, once today reaches the sixth row the already-passed first row is hidden so today remains visible.

### 3.6 Temperature/Humidity History

Shows rolling 24-hour local temperature and humidity curves. Background sensing and hourly history recording continue regardless of the visible page.

- This page can be enabled, disabled, and reordered like other pages.
- If it is the only enabled page, the device remains on it and does not create an auto-return conflict.

### 3.7 Xiaozhi AI

Provides local wake-word detection, voice conversations, on-screen transcripts, response playback, one-shot alarms, Pomodoro timers, and weather-city configuration.

- High-power voice services start only while entering the Xiaozhi page.
- First use may require binding to the Xiaozhi service by following the on-screen prompt.
- Speak the wake word while waiting. If the page says Xiaozhi is preparing, allow service initialization to finish.
- Leaving the page stops the ordinary voice session. Alarm and an active Pomodoro keep running in the background.
- This page consumes substantially more power and warms the PCB, which may make the onboard temperature/humidity reading higher than the surrounding air.

## 4. Setup Portal

### 4.1 Entering Setup

With no saved online configuration and offline mode disabled, setup starts automatically after boot.

1. Join `WeatherClock-xxxx`.
2. Wait for the captive portal or open `http://192.168.4.1/`.
3. Fill the required fields:
   - **Wi-Fi SSID:** select from the scan list or enter manually.
   - **Wi-Fi password:** password for the selected network.
   - **QWeather API Key:** required for current weather, alerts, forecast, and air quality.
   - **Weather city (optional):** for example Hangzhou. Chinese names ending in `市` are normalized and validated through QWeather. Leave empty for public-IP location.
   - **Offline date and time:** use only when Wi-Fi is intentionally left empty.

### 4.2 Online Mode

Submitting Wi-Fi credentials stores the online configuration and starts connection. QWeather API Key is required only for weather services; NTP and daily text do not use it.

After setup, NTP, weather, and daily-text requests are staggered to avoid concurrent HTTPS memory peaks. Enabled network pages receive an initial data prefetch even if they are not the first visible page.

### 4.3 Manual Weather City

A manual city takes priority over IP location and can be set through:

- The setup portal.
- Desktop-client resource configuration.
- Xiaozhi voice, for example “set the weather city to Hangzhou.”

**Settings > Network > Weather City** displays Auto or the saved city. In manual mode, press BOOT and confirm again to clear it and return to IP location. Clearing immediately queues a weather refresh.

An unrecognized QWeather city is rejected. If online validation times out, the normalized name is kept and retried during the next weather update.

### 4.4 Offline Mode

Leave Wi-Fi empty and enter a valid date/time to write the RTC and enter offline mode.

While offline:

- Wi-Fi and normal network services are stopped.
- NTP, weather, alerts, daily text, diagnostics, and OTA are unavailable.
- Weather Clock, Picture Clock, Weather Board, and Xiaozhi AI are disabled and cannot be re-enabled until online mode returns.
- Calendar, Temperature/Humidity Clock, and History remain available.
- Cached data is not actively deleted.

To leave offline mode:

- With saved Wi-Fi and QWeather API Key, turn it off directly.
- If either is missing, confirm twice to enter setup and finish online configuration.

## 5. Settings

Press **KEY** to enter Settings. The left column is the primary menu; the right side contains secondary items. The selected item is shown with inverted colors.

### 5.1 Network

- **Sync Time:** run NTP now.
- **Sync Weather:** update current weather, alerts, forecast, and air quality, refreshing the cache shared by Weather Clock and Weather Board.
- **Update Daily Text:** refresh the Picture Clock text.
- **Weather City:** inspect automatic/manual mode or clear a manual city with confirmation.

### 5.2 Sound

- **Volume:** cycle through 20%, 40%, 60%, 80%, and 100% with preview playback.
- **Sound Select:** cycle through available reminder sounds with preview playback.
- **Hourly Reminder 7:00–22:00:** chime only in the daytime window.
- **All-day Reminder 0:00–24:00:** higher priority; chime every hour all day.

When both reminder switches are off, no hourly sound plays. Critical Xiaozhi audio, OTA, and other protected operations avoid concurrent Codec use.

### 5.3 Display

- **Page switches:** enable or disable pages. Network pages are blocked while offline, and the final enabled page cannot be disabled.
- **Page order:** lists only enabled pages. KEY selects; BOOT exchanges the selected page with the next one. The first item becomes the boot home page.
- **Alarm:** displays the one-shot alarm and allows manual disable when active.
- **Xiaozhi AI auto return:** after five minutes without valid activity, return from Xiaozhi to the first page. Auto-return pauses while a Pomodoro runs.

Xiaozhi AI cannot be the first page because its high-power service is unsuitable as the boot home page. At least one non-Xiaozhi page must remain enabled.

### 5.4 System

- **Offline Mode**
- **Network Diagnostics**
- **Factory Reset** (requires confirmation)
- **About Device** (version, battery, voltage, last-charge time, device information, and source repository)
- **Check Update**

## 6. Alarm and Pomodoro

### 6.1 One-shot Alarm

The single alarm can be set, changed, or disabled through Xiaozhi voice.

- Example: “Wake me tomorrow at 6:30.”
- Replacing an existing different alarm requires explicit confirmation.
- Alarm and Pomodoro completion cannot target the same local minute; the later request is rejected.
- The alarm repeats after about five seconds for up to one minute.
- Either hardware key stops it.
- It disables itself after ringing.

### 6.2 Pomodoro

- Say “start a 25-minute Pomodoro” or “focus for 45 minutes.”
- Default is 25 minutes; valid range is 1 second to 99 minutes 59 seconds. Starting again changes the active duration.
- Ask for remaining time, or say “cancel the Pomodoro” / “end focus.”
- Ordinary “remind me in 10 minutes” requests remain alarm requests.
- It continues after changing pages but is cleared by reboot.
- In the final minute, the minute card shows `00` and the right card shows whole remaining seconds; hundredths are intentionally not displayed.
- Completion shows a completed state and plays two prompts. Either key stops playback.
- Saying only “close” or “stop” exits the Xiaozhi conversation and does not cancel a background Pomodoro.

## 7. OTA and Flashing

Open **Settings > System > Check Update**:

1. Press BOOT to check.
2. When a new version is available, press BOOT again within 60 seconds.
3. Download percentage, speed, and progress bar remain visible.
4. After validation, the device shows a reboot notice before restarting.

OTA uses a primary remote source and a scheduled backup source. The backup may lag behind shortly after a release.

OTA is blocked during offline mode, low-battery mode, setup, or another active OTA flow.

### 7.1 App bin vs. merged bin

- **App bin:** updates an existing valid OTA App partition and can preserve NVS, assets, and sensor history. Never write an App bin at `0x0`; use the correct current App partition address.
- **Merged bin:** written from `0x0` and includes bootloader, partition table, OTA data, and App. Use for first install or partition-table migration.

`v1.5.x` has a different partition layout from `v1.4.x`. Fully flash once before using normal OTA updates on that device.

## 8. Desktop-client DIY Assets

The dedicated `assets` partition supports desktop-client uploads for:

- The Weather Clock GIF region as fixed-size 1-bit animation frames.
- Picture Clock gallery images as fixed-size 1-bit bitmaps.
- Optional resource-package weather city and custom OTA endpoint settings.

The firmware validates package header, dimensions, offsets, lengths, and CRC. Missing or invalid custom data falls back to built-in assets.

Because `v1.5.0` moved partition addresses, an old desktop client must be updated to use the current partition table before writing resources.

## 9. Battery and Low-power Behavior

- Battery is sampled immediately after boot.
- When not charging, battery ADC follows the same schedule as local temperature/humidity: every minute during the day and every two minutes at night.
- During confirmed active charging, battery sampling increases to about once per second.
- Low battery enters a minimal page and stops non-essential networking, animation, audio, and high-frequency refresh.
- Charging state and percentage are estimates derived from voltage trends and are not precision battery instrumentation.

## 10. Factory Reset and Retained Data

Factory reset clears:

- Wi-Fi SSID/password.
- QWeather API Key and manual city.
- Offline mode.
- Sound, volume, and reminder settings.
- Page switches/order and Xiaozhi auto-return.
- One-shot alarm.

Factory reset retains:

- Existing Xiaozhi activation/binding data.
- Stored sensor history.
- Desktop-client DIY assets.

The device then enters setup as an unconfigured clock.

## 11. Troubleshooting

### Setup portal does not open

Stay connected to the `WeatherClock-` AP, disable automatic mobile-data switching, and browse to `http://192.168.4.1/`.

### Weather remains “Waiting for data”

Verify Wi-Fi, QWeather API Key, and city. Run **Network > Sync Weather** or use **System > Network Diagnostics** to inspect QWeather, DNS, and Internet access.

### Manual city does not work

Use a common city name. Chinese `杭州市` is normalized to `杭州`. Ambiguous names may return QWeather's first matching city; restore automatic mode or use a more specific accepted name.

### Custom image or GIF is ignored

Verify the current partition table, WCA1 package format, required dimensions, and CRC. Invalid custom resources are safely ignored.

### Xiaozhi remains unavailable or preparing

Allow Wi-Fi, model, and Codec initialization to retry. If it does not recover, leave the page and enter it again, then inspect serial logs for network, model, or audio errors.

### A just-published OTA version is not found

Check access to the primary manifest. The GitHub backup is synchronized on a schedule and may temporarily remain on the previous version.

### Time was lost and data is initially blank

With an implausible RTC time, the device first shows placeholders and attempts NTP. Local sensors sample immediately when no cached value exists. Date and network data recover after synchronization.

## 12. Safety and Use Restrictions

- Never expose Wi-Fi passwords, QWeather keys, Xiaozhi credentials, or private OTA endpoints in public logs or repositories.
- Keep stable power during OTA or full flashing.
- Xiaozhi AI draws much more current and warms the PCB; leave the page or enable auto-return when battery runtime matters.
- This project is for personal learning, research, and non-commercial use only. Commercial use is prohibited. Third-party components remain governed by their own licenses.

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

Button wakeups, alarms, Pomodoro events, and UI refreshes use thread-safe task notification internally. Interaction and response timing remain unchanged, while cross-core notification no longer relies on an unsynchronized task handle.

Settings returns to the current work page after about 30 seconds without activity. Valid operations in the page-order screen restart this timeout.

On every normal work page, the button task waits for a GPIO edge while the device is not charging, networking, playing audio, updating, provisioning, or showing an auxiliary page. Weather Clock and Temperature/Humidity Clock keep their second-level display updates, while BOOT/KEY short-press and long-press behavior remains unchanged.

## 2. Status Bar and Day Progress

Depending on the page, the status area shows date, weekday, battery, Wi-Fi, reminder, alarm, and local sensor information.

- **Wi-Fi icon:** shown whenever the Wi-Fi radio is on and hidden when it is off, including connection and synchronization periods.
- If the device cannot temporarily reserve the wake resource required for networking, that operation fails safely or retries later without forcing Wi-Fi on. Wait briefly before trying again.
- Wi-Fi name, password, and weather API key take effect as one complete configuration. If the system is temporarily busy, it keeps using the previous complete configuration instead of mixing old and new fields.
- Submitting the same online configuration again does not rewrite persistent storage. Connection, weather synchronization, and page behavior after setup remain unchanged.
- Networking and audio keep independent nested wake-resource ownership. If one initialization step fails, resources acquired by that attempt are rolled back instead of permanently blocking the normal low-power state.
- During automatic startup weather or daily-saying refresh, insufficient contiguous memory postpones the saved task for about 10 seconds. Manual synchronization and the first synchronization after provisioning are not postponed by this automatic gate.
- Current weather, alerts, forecasts, and air quality are published as one consistent snapshot. If an extended endpoint temporarily fails, the last valid extended data remains visible instead of mixing a partial update into the page.
- **Sound icon:** shown when an hourly or all-day reminder is enabled.
- **Alarm icon:** shown while the one-shot alarm is enabled.
- Every work page uses the same date, local sensor, minute-time, sound, Wi-Fi, and alarm state sources through one per-page status-bar registry. Switching pages or rebuilding the UI does not change their content, position, visibility rules, or icon-buffer reuse.
- Work pages, System Info, Network Diagnostics, and Settings share one page-lifecycle manager for switching and UI rebuilding. This structural maintenance does not change the seven work pages or the entry, return, and timeout behavior of auxiliary pages.
- Shared bitmap, digit-clock, day-progress, OTA-panel, visible-data-sync, and base-widget interfaces now declare only their actual dependencies. This internal maintenance reduces unrelated coupling without changing page pixels, refresh timing, network rules, or controls.
- Provisioning, weather, NTP, HTTP, and background synchronization keep the same public behavior while their internal helpers now declare dependencies directly. Request order, weather data, failure handling, and controls are unchanged.
- **Battery icon:** shows the shared battery state on all seven work pages while refreshing only the visible page. During detected charging it blinks on whole-second boundaries. The hardware has no dedicated CHG/VBUS input, so plug/unplug detection based on ADC voltage trends can be delayed briefly.
- **Day-progress strip:** shared by all seven pages. Its 60 segments each represent about 24 minutes and refresh only on page entry or when crossing a new segment.
- Every page uses the same internal drawing and boundary rules for this strip. Segment count, placement, refresh timing, and visible behavior remain unchanged.

The display favors partial refreshes. Second-level pages redraw only changing digits or small regions; low-frequency pages wait until the next minute, date, sensor, or network-data change before waking for an update.

## 3. Seven Work Pages

Press **BOOT** to follow the saved page order. Disabled pages are skipped. Every page can be disabled, but the device prevents disabling the final enabled page.

The seven page identities now share one internal definition across display, storage, and tests. This maintenance does not change the saved order, page switches, controls, or upgrade data.

### 3.1 Weather Clock

Shows large time, date, battery, current weather, alerts, local temperature/humidity, trend arrows, status icons, and an animation area.

- Time and required animation regions use second-level partial refresh.
- Date, minute time, seconds, animation, and second progress use their own change-driven partial refresh rates; hourly chimes continue to follow the saved settings.
- Entering the page requests weather only when required data is missing or stale.
- Offline, low-battery, setup, and OTA states block ordinary weather requests. Low-battery and setup screens may reuse the Weather Clock layout, but that display fallback does not turn on networking.

### 3.2 Picture Clock

Shows a local image, large time, daily text, and local temperature/humidity summary.

- Built-in images are selected by weekday and change once per day.
- When a desktop-client gallery is installed, custom images take priority and rotate daily by date.
- Daily text is fetched only when the Picture Clock needs it and is not repeatedly downloaded after a successful update for the same day.
- Rebuilding the page redraws its image, time, and daily text without changing selection rules or controls.

### 3.3 Weather Board

Shows city, current temperature and condition, air quality, humidity, wind, sunrise/sunset, time until the next sunrise or sunset, weather alerts, and a six-day forecast.

- Missing or stale data causes a full weather refresh when entering the page.
- Sunrise/sunset countdown updates once per minute.
- Local sensor and status text refresh only when their values change.
- Current conditions, alerts, forecast, air quality, and sunrise/sunset formatting now share the same internal production weather types and capacities, preventing parser/display size drift without changing content, synchronization, or controls.

### 3.4 Temperature/Humidity Clock

Shows three high-contrast hour/minute/second cards plus local temperature, humidity, trend arrows, comfort faces, date, and lunar date.

- Seconds refresh locally once per second; ordinary ticks update only the changed digit region instead of redrawing the whole card. The page sleeps until the next wall-clock second instead of polling repeatedly between ticks.
- Local temperature/humidity is sampled every minute during the day and every two minutes at night.
- Trend arrows use the rolling average of valid samples from the latest four-hour window and restart after reboot.
- Internal page construction and runtime refresh are isolated. A page rebuild restores all clock cards, sensor, trend/comfort, date, and lunar objects while reusing pixel buffers; display, refresh frequency, controls, and stored data are unchanged.

### 3.5 Calendar

Shows the current month, weekday bar, lunar text, and holidays.

- Manual date/time entry, RTC validity, and the lunar calendar currently share the supported range `2024–2035`; dates outside this range are not accepted as valid device time.
- The calendar body redraws only on page entry, date/month change, or a time correction that crosses a day boundary.
- The first frame after a page rebuild always redraws the calendar instead of reusing the previous page's date cache.
- For rare six-row months, once today reaches the sixth row the already-passed first row is hidden so today remains visible.

### 3.6 Temperature/Humidity History

Shows rolling 24-hour local temperature and humidity curves. Background sensing and hourly history recording continue regardless of the visible page.

- This page can be enabled, disabled, and reordered like other pages.
- If it is the only enabled page, the device remains on it and does not create an auto-return conflict.
- Hourly samples are published to the chart only after a complete save. History readers cannot observe a mixture of old and new slots, while existing NVS records and upgrade compatibility remain unchanged.
- After page objects are rebuilt, the chart, axes, and extrema redraw immediately instead of waiting for the next hourly sample.

### 3.7 Xiaozhi AI

Provides local wake-word detection, voice conversations, on-screen transcripts, response playback, one-shot alarms, Pomodoro timers, and weather-city configuration.

- High-power voice services start only while entering the Xiaozhi page.
- While waiting for cloud reply audio, an empty playback queue sleeps until new data arrives instead of polling every few milliseconds. Voice content, initial buffering, and playback order are unchanged.
- First use may require binding to the Xiaozhi service by following the on-screen prompt.
- A bound device restores its saved service configuration directly. An unbound device still displays and announces the binding ID first. It is marked announced only after the prompt and all digits finish; if audio is temporarily busy or the task cannot start, a later activation cycle retries without requiring page switching or a reboot.
- An unbound device or failed service activation still retries at the existing interval. Request and response scratch memory is cleared and reused between attempts to reduce long-running heap fragmentation without changing the binding procedure or prompts.
- When the service repeats an identical activation configuration, the device reuses the stored values instead of writing them to flash again. Binding recovery, rebinding, and factory-reset retention behavior are unchanged.
- Speak the wake word while waiting. If the page says Xiaozhi is preparing, allow service initialization to finish.
- If the cloud service recognizes only an incomplete phrase and returns no text or audio reply, the page shows that it did not hear the complete request. Continue or repeat the request; about 12 seconds of silence returns to wake-word standby.
- User transcripts, assistant replies, emotions, and playback completion use one conversation-state path so multi-turn listening, farewell return, and minimum transcript visibility remain consistent.
- Xiaozhi page state is published as a complete snapshot before the UI is notified. If the resource is temporarily busy, the previous complete state remains visible instead of mixing partial new and old text.
- Service handshakes, ordinary replies, and MCP tool messages share a bounded session buffer. An invalid oversized text frame ends the current session instead of overwriting adjacent memory.
- Wake-word listening starts only after Xiaozhi has acquired the microphone and audio hardware. If an alarm, Pomodoro alert, or prompt is using audio, the existing recovery path retries after the hardware is released instead of starting a partial listener.
- Device-status queries format sensor, battery, and volume data in a fixed-capacity buffer. Under memory pressure, the current query fails cleanly without retaining temporary memory or disrupting later conversations.
- Xiaozhi validates audio lengths before sending, decoding, sample-rate conversion, and playback queueing. An invalid frame ends only the current conversation instead of reading beyond its buffer.
- WebSocket receive, Opus encode, and audio-decode scratch now share one PSRAM lease across conversations and are cleared before and after each one. The encoder no longer allocates a separate buffer per conversation; microphone, speaker, Wi-Fi, and voice tasks still stop after leaving the Xiaozhi page.
- The short first-binding code now crosses into its playback task through a fixed slot protected by an application-lifetime static task mutex, avoiding both per-announcement heap allocation and interrupt masking during string copies. Prompt and digit order are unchanged, and a failed playback remains eligible for a later activation retry.
- Page status, subtitles, and emotion refresh only when their content changes. Offline, unconfigured, or retry states do not repeatedly redraw the same message.
- After setup transitions, page-resource recovery, or a full UI rebuild, Xiaozhi status, emotion, and an active Pomodoro are immediately restored on the new page objects instead of retaining stale display references.
- While offline mode is enabled or Wi-Fi has not been saved, Xiaozhi waits for a configuration change instead of periodically starting network work. Saving setup, changing offline mode, or leaving the page wakes it immediately.
- Binding or service connection failures retry automatically at about 15-second intervals. Leaving the page, alarms, and Pomodoro events remain immediately responsive during this wait.
- Leaving the page or reaching a failed connection retry stops the ordinary voice session and releases its page-owned network/power resources. Alarm and an active Pomodoro keep running in the background.
- Audio hardware still shuts down after prompts, alarms, chimes, and Xiaozhi sessions. Repeated audio use reuses fixed object storage to reduce long-running heap fragmentation without keeping the codec powered while idle.
- Xiaozhi reply queues and playback tasks still run only during a conversation. Their small control metadata now reuses fixed storage to reduce internal-heap fragmentation across repeated conversations without changing page-exit cleanup.
- The intermediate wake-word and speech-processing queue, plus the two recognition tasks' small control blocks, reuse fixed storage. Their larger task stacks still exist only while the Xiaozhi page is active and are released on exit. Leaving the page still stops the microphone, recognition tasks, and audio hardware; fixed control metadata does not mean the device keeps listening.
- This page consumes substantially more power and warms the PCB, which may make the onboard temperature/humidity reading higher than the surrounding air.

## 4. Setup Portal

### 4.1 Entering Setup

With no saved online configuration and offline mode disabled, setup starts automatically after boot.

1. Join `WeatherClock-xxxx`.
2. Wait for the captive portal or open `http://192.168.4.1/`.
3. Fill the required fields:
   - **Wi-Fi SSID:** select from the scan list or enter manually. The list shows up to 32 access points; refreshing scans again, and a temporary memory warning does not prevent manual entry.
   - **Wi-Fi password:** password for the selected network.
   - **QWeather API Key:** required for current weather, alerts, forecast, and air quality.
   - **Weather city (optional):** for example Hangzhou. Chinese names ending in `市` are normalized and validated through QWeather. Leave empty for public-IP location.
   - **Offline date and time (optional):** use only when Wi-Fi is intentionally left empty. Leave it blank for normal Wi-Fi setup.

After Save is pressed, the page immediately shows a validating state; validation does not intentionally restart the device. A background network task connects to the selected router in AP+STA mode, validates the QWeather API key against the current-weather service, and then validates an optional manual city. The page polls the lightweight validation status. The setup hotspot closes only after all checks pass. A Wi-Fi password, API key, or city failure keeps the hotspot active and shows a specific error. If the phone loses the current HTTP connection while the STA changes channel, reopening `http://192.168.4.1/` shows the latest validation state above the form. The Save button remains on its own row below the date/time field.

When setup starts, the device first presents the setup overlay and then plays the provisioning prompt. Normal prompts initialize speaker output only and do not allocate microphone input DMA. If the first display frame temporarily consumes the remaining DMA memory, prompt playback waits briefly and retries a bounded number of times. The setup view keeps RTC-restored hours/minutes and date visible while second animation, GIF, weather, and lower work-page refreshes stay paused.

In the rare case that the platform cannot configure the captive DNS receive timeout, the page may not open automatically. Open the address above manually; the failed DNS service exits cleanly instead of remaining active after setup.

When the last phone leaves the setup hotspot, the device resets the captive DHCP lease state. A phone that reconnects can therefore obtain a fresh `192.168.4.x` address. If the captive page does not reopen automatically, visit `http://192.168.4.1/` directly.

The on-device setup status rows follow setup-mode visibility as one panel. If the UI is rebuilt after a mode switch or resource recovery, those rows are recreated and continue refreshing without retaining stale object references.

Setup-field decoding, configuration-event cleanup, configuration storage, and factory-reset cleanup use lightweight internal helpers. This maintenance does not change field names, Chinese text handling, truncation feedback, save results, existing configuration recovery, or button controls.

### 4.2 Online Mode

Submitting Wi-Fi credentials stores the online configuration and starts connection. QWeather API Key is required only for weather services; NTP and daily text do not use it. If the short boot request obtains current conditions but not forecast or air quality, a staggered background refresh remains scheduled so extended weather-board data is normally ready before first entry.

The device publishes the Wi-Fi name, password, and weather API key to background tasks as one complete configuration. Saving cannot mix old and new credential fields, and serial logs do not print the password or full API key. Setup steps and field formats are unchanged.

After setup, NTP, weather, and daily-text requests are staggered to avoid concurrent HTTPS memory peaks. Enabled network pages receive an initial data prefetch even if they are not the first visible page.

Regular HTTPS synchronization and Xiaozhi WebSocket connection setup share one serialized network boundary. If they overlap, the later operation waits for the active transaction to finish instead of competing for TLS memory. User-facing synchronization, OTA, and Xiaozhi controls are unchanged.

The clock synchronizes time once at startup and then at local midnight each day. A failed midnight synchronization remains pending and retries after the normal delay, so crossing past 00:00 does not discard the daily update.

A manual time synchronization is an explicit user request and bypasses any retry deadline left by an earlier startup or midnight failure. If it fails, the settings page reports the result without scheduling an otherwise unused background wake-up. Startup and midnight synchronization retain automatic retries: about 15 seconds while RTC time is implausible and about 5 minutes after time is already valid.

### 4.3 Manual Weather City

A manual city takes priority over IP location and can be set through:

- The setup portal.
- Desktop-client resource configuration.
- Xiaozhi voice, for example “set the weather city to Hangzhou.”

**Settings > Network > Weather City** displays Auto or the saved city. In manual mode, press BOOT and confirm again to clear it and return to IP location. Clearing immediately queues a weather refresh.

An unrecognized QWeather city is rejected while the setup hotspot remains active. The user can correct the city or clear it to restore automatic IP location; setup does not silently replace an invalid manual choice.

After a city is saved, it takes effect as one complete text snapshot without a reboot across setup, Xiaozhi, Settings, and weather synchronization. Updating a Chinese city while a sync is running cannot expose a partial or mixed city name.

The setup portal, desktop-client resource, Xiaozhi, saved configuration, and Settings display use the same city-length and validation limits. A city is therefore not accepted by one entry path only to be truncated differently by another.

When cities are set repeatedly through Xiaozhi, each confirmed request has its own generation and the newest request is retained even when two consecutive names are identical. After the spoken reply finishes, current conditions, alerts, forecast, and air quality are refreshed through the existing background flow.

### 4.4 Offline Mode

Leave Wi-Fi empty and enter a valid date/time to write the RTC and enter offline mode.

While offline:

- Wi-Fi and normal network services are stopped.
- NTP, weather, alerts, daily text, diagnostics, and OTA are unavailable.
- Weather Clock, Picture Clock, Weather Board, and Xiaozhi AI are disabled and cannot be re-enabled until online mode returns.
- Calendar, Temperature/Humidity Clock, and History remain available.
- Cached data is not actively deleted.
- Offline state is saved across restarts. Settings, page availability, weather, OTA, and Xiaozhi immediately use the same state without requiring it to be enabled again.

To leave offline mode:

- With saved Wi-Fi and QWeather API Key, turn it off directly.
- If either is missing, confirm twice to enter setup and finish online configuration.

## 5. Settings

Press **KEY** to enter Settings. The left column is the primary menu; the right side contains secondary items. The selected item is shown with inverted colors.

Settings navigation and confirmation state are handed off safely between input, UI, and OTA tasks. Primary/secondary focus, page-manager mode, and all selection indexes are updated as one complete state, so rapid key use cannot briefly combine the wrong focus and item. Returning from About Device or Network Diagnostics and keeping the update panel visible also cannot reuse navigation state from an earlier path. Key behavior and the 30-second inactivity timeout are unchanged.

Save, synchronization, timeout, and confirmation feedback is published together with its display deadline. The active synchronization type and its 60-second timeout are also handed off as one complete state. Rapid input, a late background result, or synchronization completion cannot expose partial text, mix old and new deadlines, or finish a different operation; wording, duration, and controls are unchanged.

When network settings are saved or restored, Wi-Fi data and the weather API key become active as one complete configuration. Background tasks never consume a half-updated configuration, and passwords or API keys are never written to the serial log.

### 5.1 Network

- **Sync Time:** run NTP now.
- **Sync Weather:** update current weather, alerts, forecast, and air quality, refreshing the cache shared by Weather Clock and Weather Board.
- **Update Daily Text:** refresh the Picture Clock text.
- **Weather City:** inspect automatic/manual mode or clear a manual city with confirmation.

Each network window reports the requests captured when it started. A new request raised while that window is running is preserved for the next window instead of being cleared by the earlier result.

### 5.2 Sound

- **Volume:** cycle through 20%, 40%, 60%, 80%, and 100% with preview playback.
- **Sound Select:** cycle through available reminder sounds with preview playback.
- **Hourly Reminder 7:00–22:00:** chime only in the daytime window.
- **All-day Reminder 0:00–24:00:** higher priority; chime every hour all day.

When both reminder switches are off, no hourly sound plays. Critical Xiaozhi audio, OTA, and other protected operations avoid concurrent Codec use.
If the device cannot temporarily reserve the wake and clock resources required for playback, that sound is skipped safely and its playback state is released. Later reminders and previews can try again normally.
When audio is busy, rapid repeated sound-setting changes keep only one pending preview. Once playback becomes available, the preview uses the latest selection instead of replaying every intermediate choice.
No vendor audio demo task runs in the background. Audio resources are opened only for actual chimes, previews, alarms, Pomodoro completion, or Xiaozhi sessions and are released through the shared lifecycle afterward.
The button-task and OTA entry contracts no longer pull unrelated page, network, or global-state implementation details into their callers. Button controls and the update check, download, validation, and reboot flow are unchanged.
Codec and sensor I2C register writes no longer allocate internal heap memory on every call, reducing transient fragmentation during repeated previews or Xiaozhi startup. Audio, sensor, and page controls are unchanged.
Xiaozhi shutdown diagnostics now use a separately synchronized audio-resource state instead of reading a Codec pointer while another task may create or release it. This improves diagnostic stability without changing sound controls or use.
Hourly chimes, previews, alarms, Pomodoro completion, and Xiaozhi share one atomic playback claim. Concurrent sounds still allow only one Codec owner, while frequent busy-state checks no longer enter a cross-core critical section.
The audio layer validates speaker and microphone readiness separately. A partial Codec startup cannot publish an unusable audio resource or pass a missing microphone handle to the driver; the current attempt ends safely and a later reminder or Xiaozhi entry may retry.
If no valid playback handle can be created, the device parks the audio pins and releases the high-performance power resources before returning from that attempt, so one initialization failure cannot leave sustained extra power draw.
Production audio now reuses the ES8311/ES7210 I2C controls already owned by the board Codec layer instead of registering duplicate, unused device handles for every session. Sound behavior and hardware addresses are unchanged.
The network transaction lock shared by weather, daily text, Xiaozhi, and OTA is now a static lifetime resource. This removes a cold-start heap allocation and a source of long-lived fragmentation without changing request order, timeouts, or user operation.
The fixed capacities for OTA version, download URL, and SHA256 metadata are now owned by the OTA module, so the pure parser interface no longer carries unrelated display or application-state dependencies. Manifest format, source priority, download validation, and user operation are unchanged.
Daily text and its successful synchronization time are now published as one consistent snapshot, preventing a page refresh from pairing new text with an older timestamp. The internal cache remains 160 bytes; the 22-character limit, fetch timing, and display behavior are unchanged.
The depth counter mutex used by network and audio power locks is also a static lifetime resource. This reduces startup heap allocation and long-lived fragmentation without changing light sleep, network, or audio behavior.
Current local temperature, humidity, trends, and refresh version are also published as one task-level snapshot, so pages and Xiaozhi cannot observe fields from different sampling batches. Sampling intervals, arrows, and display behavior are unchanged.
Internal sensor, battery, hourly-history, wall-clock, and power-lock interfaces now declare their own actual dependencies. This reduces unrelated maintenance coupling without changing sampling cadence, RTC behavior, charging detection, light sleep, or displayed readings.
Xiaozhi internals now read the application log tag and version through a lightweight metadata contract instead of also importing unrelated display, weather, and system-state declarations. Binding, wake-up, conversation, MCP, audio, networking, and page behavior are unchanged.
Shared audio and chime orchestration also declare only the logging, task, battery, and hardware contracts they actually use instead of importing unrelated display, weather, network, and OTA internals. Hourly chimes, setting previews, provisioning prompts, Xiaozhi audio, power behavior, and controls are unchanged.
Weather location text and QWeather response handling now load only the weather types, JSON, buffer, and logging contracts they actually use instead of unrelated display, OTA, audio, and system state. City resolution, weather data, failure messages, and page output are unchanged.
The four-hour trend samples and 48-slot hourly history now share one stable production data definition, and host validation uses the real firmware layout instead of a parallel test copy. NVS data, restart recovery, history charts, and page operation are unchanged.
The application event identifiers and event-group resource shared by provisioning, synchronization, OTA, and startup are managed by one internal owner and remain a static lifetime resource. Calls made before initialization or after startup-failure cleanup fail safely instead of touching an invalid handle. Event delivery, wait timeouts, and user operation are unchanged.
The Xiaozhi page snapshot lock also uses a static control block, reducing startup internal-memory allocation and long-term fragmentation without changing wake-up, subtitles, expressions, Pomodoro, or page controls.
The internal Xiaozhi event group used for page activity, wake-up, and suspension also uses a static control block, further reducing startup allocation without changing page transitions, alarms, or Pomodoro behavior.
If button GPIO setup fails during a rare hardware initialization fault, the firmware now shuts down the button task cleanly while leaving other background services intact instead of returning directly from a FreeRTOS task entry. Normal button, debounce, and page-switch behavior is unchanged.
RTC and SHTC3 setup also rejects an unavailable shared I2C bus. The display and shared I2C bus each have one firmware owner, and RTC, the temperature/humidity sensor, and audio always reuse the same I2C bus instead of constructing parallel application buses. The sensor module privately owns the application-lifetime SHTC3 object in static storage to reduce startup heap-allocation failure and long-term fragmentation, while complete destruction still releases its owned device handle. Each sample starts only after a confirmed wake command and verifies that the sensor returns to sleep, with one short retry for a transient sleep-command failure to avoid excess idle power. GPIO assignments, sensor addresses, first-sample ordering, intervals, and displayed readings are unchanged.

### 5.3 Display

- **Page switches:** enable or disable pages. Network pages are blocked while offline, and the final enabled page cannot be disabled. Changes are handed safely to page switching and background network decisions without changing display or sync rules.
- **Page order:** lists only enabled pages. KEY selects; BOOT exchanges the selected page with the next one. The first item becomes the boot home page. An application-lifetime static task mutex publishes the complete order as one snapshot, so rapid sorting or page switching cannot observe a half-finished exchange or require interrupt masking during order checks.
- **Alarm:** displays the one-shot alarm and allows manual disable when active.
- **Xiaozhi AI auto return:** after five minutes without valid activity, return from Xiaozhi to the first page. Auto-return pauses while a Pomodoro runs.

Xiaozhi AI cannot be the first page because its high-power service is unsuitable as the boot home page. At least one non-Xiaozhi page must remain enabled.

### 5.4 System

- **Offline Mode**
- **Network Diagnostics**: checks local IP, public IP, IP location, DNS, QWeather, NTP, Daily Saying, internet access, and the OTA manifest. The local IP and all nine results are published as one consistent snapshot and update without a reboot, so connection, lease, disconnection, or background updates never expose a partial address or status line.
- **Factory Reset** (requires confirmation)
- **About Device** (version, battery, voltage, last-charge time, device information, and source repository)
- **Check Update**

About Device and Network Diagnostics each maintain their own dynamic content. After a UI rebuild they recreate the current device information and latest diagnostics snapshot without changing entry, long-press return, or post-check timeout behavior.

The idle, running, and completed diagnostics states now use the same internal typed snapshot, preventing maintenance-time numeric state mismatches without changing the nine checks, their display order, or any controls.

The settings menu, feedback line, and update progress panel are likewise maintained by their owning UI modules. Returning from setup, About Device, or Network Diagnostics rebuilds them from current state without changing item order, the 30-second timeout, OTA percentage, speed, or progress bar.

Settings menu indexes, item counts, and manual synchronization operations now share one internal definition. This prevents display, key handling, and network actions from drifting apart without changing any visible menu order, labels, or controls.

## 6. Alarm and Pomodoro

### 6.1 One-shot Alarm

The single alarm can be set, changed, or disabled through Xiaozhi voice.

- Example: “Wake me tomorrow at 6:30.”
- Replacing an existing different alarm requires explicit confirmation.
- Alarm and Pomodoro completion cannot target the same local minute; the later request is rejected.
- The enabled alarm keeps running in the background while the device sleeps between minute boundaries. NTP or manually corrected time automatically reschedules it.
- The alarm repeats after about five seconds for up to one minute.
- Either hardware key stops it.
- It disables itself after ringing.
- Saving an identical alarm state does not perform another Flash commit; set, disable, and reboot-restore behavior is unchanged.
- Alarm enablement, ringing state, time, and replacement confirmation are updated as one thread-safe state. Status icons use a lightweight enablement read, while rapid voice replacement, background triggering, and page refresh cannot observe mixed alarm data.

### 6.2 Pomodoro

- Say “start a 25-minute Pomodoro” or “focus for 45 minutes.”
- Default is 25 minutes; valid range is 1 second to 99 minutes 59 seconds. Starting again changes the active duration.
- Ask for remaining time, or say “cancel the Pomodoro” / “end focus.”
- If the Pomodoro finishes while Xiaozhi is visible, voice listening pauses only for the completion sound. The service is not restarted while paused, and wake-word standby resumes directly afterward.
- Ordinary “remind me in 10 minutes” requests remain alarm requests.
- It continues after changing pages but is cleared by reboot.
- Pomodoro state, monotonic deadline, and completion time are published as one thread-safe snapshot. Page changes, concurrent background work, and NTP corrections cannot expose mixed timer state; countdown behavior and normal-clock restoration after completion or cancellation are unchanged.
- In the final minute, the minute card shows `00` and the right card shows whole remaining seconds; hundredths are intentionally not displayed.
- Completion shows a completed state and plays two prompts. Either key stops playback.
- Saying only “close” or “stop” exits the Xiaozhi conversation and does not cancel a background Pomodoro.

## 7. OTA and Flashing

For each public source release, GitHub Actions automatically attaches two build outputs to the matching GitHub Release: `weather_clock_vX.X.X.bin` for OTA or app-partition flashing, and `weather_clock_vX.X.X_merged.bin` for a complete flash from address `0x0`. The automated build only adds firmware assets and does not rewrite the complete source Release notes. The source tag also contains one bounded, numbered OTA summary shared by the Cloudflare manifests and the GitHub OTA fallback repository.

After the GitHub build completes, the Cloudflare OTA service imports and verifies both files automatically. If the automatic notification is delayed, the maintenance release flow requests a protected retry. The previous online manifest remains active until both new firmware images pass validation.

The GitHub OTA fallback repository is updated from the same source build. The source repository dispatches an event after its app, merged image, and manifests are ready; the fallback repository then downloads both Release assets, verifies size and SHA256, and only afterward updates its own Releases and manifests. It no longer polls Cloudflare on a daily schedule, and fallback manifest URLs point to the fallback repository's own Release assets.

Both automation paths now share one firmware-artifact naming and validation contract, preventing the app and complete flash image from drifting apart. Existing filenames, verification, device OTA steps, and serial flashing instructions are unchanged.

Internally, provisioning, offline mode, chime, volume, and Xiaozhi auto-return settings are safely published to background tasks. Xiaozhi auto-return now has one dedicated runtime state shared by Settings, storage, and the five-minute decision, without changing where it is edited, how it is saved, or how it is restored after restart.

OTA check state, download progress, speed, and reboot notices are likewise published as one consistent snapshot between background tasks and the UI. The check, confirmation, download, and restart workflow is unchanged.

The online manifest may include release notes for publishing tools and the desktop client. The device retains only the version, download URL, file size, and SHA256 metadata required for installation instead of keeping unused release-note text in memory.
OTA manifests and the GitHub OTA fallback Release use the same bounded summary from the source tag. Complete numbered notes remain in the matching Gitea/GitHub source Release so long descriptions cannot interfere with device update checks.

Open **Settings > System > Check Update**:

1. Press BOOT to check.
2. When a new version is available, press BOOT again within 60 seconds.
3. Download percentage, speed, and progress bar remain visible.
4. After validation, the device shows a reboot notice before restarting.

The firmware follows OTA download redirects and closes the current HTTP connection on failure or early exit. A failed download does not switch the boot partition and can be retried.

OTA uses a primary remote source and an event-driven GitHub fallback. The fallback may briefly remain on the previous version while source build and mirror validation are still running.

OTA is blocked during offline mode, low-battery mode, setup, or another active OTA flow.

While an OTA check or download is active, ordinary background network synchronization sleeps until the OTA state changes. Download progress continues to update normally without repeatedly waking unrelated weather, time, or daily-text work.

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

Internal resource-loader dependencies and duplicate diagnostic declarations have been simplified without changing the package format, validation sequence, messages, desktop-client interface, or built-in fallback behavior.

Because `v1.5.0` moved partition addresses, an old desktop client must be updated to use the current partition table before writing resources.

## 9. Battery and Low-power Behavior

- Battery is sampled immediately after boot.
- When not charging, battery ADC follows the same schedule as local temperature/humidity: every minute during the day and every two minutes at night.
- Temperature and humidity samples notify the active page for an on-demand update. Stable text is not redrawn repeatedly; a roughly one-minute fallback check remains for resilience.
- During confirmed active charging, battery sampling increases to about once per second.
- Low-battery thresholds, charging detection, animation stopping, and fast charging sampling now share one internal policy source. This maintenance change does not alter the displayed percentage, sampling cadence, charging indication, low-battery behavior, or OTA protection.
- Each battery reading releases the ADC and calibration resources after publishing the result, so the measurement peripheral is not kept active between samples.
- Percentage, charging state, and low-battery mode are published consistently from the same sample; pages, OTA, and Xiaozhi do not trigger an extra ADC conversion when reading battery status.
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

The startup screen, on-device setup status, and phone portal all show the same complete name of the active setup AP. If the name is blank or inconsistent, re-enter setup mode and retain the serial log.

If portal startup fails, the device removes the incomplete AP and restores its previous Wi-Fi mode instead of leaving an unusable high-power hotspot active. Retry setup after a short wait.

### Weather remains “Waiting for data”

Verify Wi-Fi, QWeather API Key, and city. Run **Network > Sync Weather** or use **System > Network Diagnostics** to inspect QWeather, DNS, and Internet access.

If the display is rebuilt after setup-mode switching or display-resource recovery, the weather city, status, icon, temperature, and humidity are repopulated on the new page objects. Existing cached weather is not cleared by a UI rebuild.

The weather clock's local temperature, humidity, and trend arrows are also restored on the rebuilt page. A UI rebuild does not clear sensor samples or restart the four-hour trend calculation.

The top date, weather alert, and sound, Wi-Fi, and alarm icons are restored from their current state after the rebuild. Rebuilding the UI does not change alert rotation or the enabled state of those features.

The main time, seconds, status animation, second progress, and low-battery indicator are also restored after a UI rebuild and continue using the same minute, second, and partial-refresh rules.

### Manual city does not work

Use a common city name. Chinese `杭州市` is normalized to `杭州`. Ambiguous names may return QWeather's first matching city; restore automatic mode or use a more specific accepted name.

### Custom image or GIF is ignored

Verify the current partition table, WCA1 package format, required dimensions, and CRC. Invalid custom resources are safely ignored.

### Xiaozhi remains unavailable or preparing

Allow Wi-Fi, model, and Codec initialization to retry. If the main task cannot start because resources are temporarily unavailable, it retries about every five seconds without requiring page switching. If it does not recover, leave the page and enter it again, then inspect serial logs for network, model, or audio errors.
After Wi-Fi startup or connection timeout, the device retries in about 15 seconds. During that wait it releases the failed session's network and voice resources instead of remaining at conversation power.
An occasional microphone read error is retried briefly. If reads remain unavailable for about one second, the device exits that failing capture loop and automatically rebuilds voice listening instead of staying in a high-frequency error state.
When an abnormal listener is stopped, its capture buffer is also released centrally, so repeated automatic recovery does not keep consuming additional PSRAM.
If temporary memory pressure prevents a Xiaozhi tool response from being built, the device discards that response and releases its temporary data safely. Retry the request after the service recovers; a reboot is not required.
If a speaker write fails, the current voice session ends immediately, queued audio is released, and listening is rebuilt automatically instead of remaining in the speaking state until timeout.
After Xiaozhi changes the weather city, the device releases real-time voice resources before refreshing all weather data. A network fault cannot leave Xiaozhi waiting forever; the queued refresh remains owned by the background network task.

### A just-published OTA version is not found

Check access to the primary manifest. The GitHub fallback is synchronized by the source-build completion event and may briefly remain on the previous version while the build or mirror job is still running.

If the serial log shows both `OTA manifest source skipped: R2` and `OTA manifest source skipped: GitHub`, the firmware was built without production OTA endpoints and did not make an HTTP request. This is not caused by Wi-Fi or manifest length. Recover by flashing a corrected App image over serial while preserving NVS, or by provisioning a valid custom OTA endpoint with the desktop client.

### Time was lost and data is initially blank

With an implausible RTC time, the device first shows placeholders and attempts NTP. Local sensors sample immediately when no cached value exists. A failed automatic NTP attempt retries after about 15 seconds, while a manual request bypasses that delay. Successful synchronization immediately refreshes date, weekday, and time; staggered weather and daily text then continue in the background. Repeated `task_wdt` reports naming `network_sync` indicate an abnormal busy loop rather than normal waiting and should be retained for diagnosis.

The last-sync value shown under About Device is read from the same task-mutex snapshot that the network task updates. SNTP waiting, RTC writes, and UI notifications remain outside that mutex, so viewing the value cannot extend a sync attempt or change its result.

### Startup screen does not continue

In the rare event of a display-resource or panel-register startup failure, the firmware releases any acquired SPI bus, panel interface, reset GPIO, display buffers, lookup tables, LVGL buffers, timer, and lock instead of rebooting repeatedly or continuing periodic wake-ups in an unusable state. The long-lived LVGL display lock, handler-task stack, and task control block use static storage to reduce internal-heap allocation and long-term fragmentation at startup; page, button, and refresh behavior are unchanged. Power-cycle the device; if it still cannot enter a work page, inspect the serial log for `RLCD display resources unavailable`, `RLCD panel register initialization failed`, or `LVGL initialization failed` and the preceding specific error.

If the shared I2C master bus itself cannot be created, startup stops before RTC, sensor, audio, networking, and application tasks are initialized instead of entering a reset loop. Power-cycle the device and inspect the serial log for `I2C master bus unavailable` and the preceding driver error. A missing individual RTC or temperature/humidity sensor remains a separate recoverable device error and does not by itself stop the clock.

During normal operation, a temporary SPI display allocation or timeout error is retried within a fixed limit. If it still fails, only that frame is skipped and `RLCD command/data tx failed` is logged instead of rebooting the device. The network/OTA DMA protection mode uses an allocation-free atomic snapshot, so display transfers no longer disable cross-core interrupts merely to read the active protection tier; chunk sizes, retry counts, and rendered output are unchanged. Repeated messages warrant checking power stability and the logged DMA headroom.

Shared bitmap, label, and font declarations use lightweight internal interfaces. This maintenance does not change Chinese glyphs, icon pixels, page coordinates, trend arrows, or partial-refresh behavior.

## 12. Safety and Use Restrictions

- Never expose Wi-Fi passwords, QWeather keys, Xiaozhi credentials, or private OTA endpoints in public logs or repositories.
- Keep stable power during OTA or full flashing.
- Xiaozhi AI draws much more current and warms the PCB; leave the page or enable auto-return when battery runtime matters.
- This project is for personal learning, research, and non-commercial use only. Commercial use is prohibited. Third-party components remain governed by their own licenses.

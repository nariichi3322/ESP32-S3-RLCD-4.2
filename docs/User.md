# Weather Clock User Guide

## Online and offline operation

Settings mode offers two independent paths. For online use, enter a primary Wi-Fi network and optionally a backup network, NTP server, and weather city. Saving a valid online configuration exits offline mode and schedules time, weather, and Daily Saying synchronization.

For offline use, enter the current local date and time. The device stores offline mode, stops Wi-Fi, blocks background network work, and hides network-dependent pages. Offline mode can be disabled directly when a valid Wi-Fi configuration is already stored; otherwise the device opens Settings mode.

## Weather

Weather data is supplied by Open-Meteo. No API key or custom host is required. A manually entered city is resolved with the Open-Meteo Geocoding API. If the city field is empty, the device obtains coordinates from public-IP location and sends those coordinates directly to Open-Meteo.

The Weather Board displays current conditions, a six-day forecast, and current air quality. Weather alerts are not displayed because they are not part of this integration. Weather codes follow the WMO interpretation table and use provider-neutral monochrome icons.

Weather data: [Open-Meteo Forecast API](https://open-meteo.com/en/docs) and [Geocoding API](https://open-meteo.com/en/docs/geocoding-api). Air-quality data includes CAMS-derived information through the [Open-Meteo Air Quality API](https://open-meteo.com/en/docs/air-quality-api). Please retain these attributions when redistributing the firmware or screenshots containing weather data.

## CODEX Usage Bluetooth

The CODEX page is controlled by the common page visibility and order settings. Bluetooth starts only while the normal CODEX page is visible. Leaving that page, entering low-battery mode, Settings, or another auxiliary page stops Bluetooth and clears connection and pairing-overlay state. The Bluetooth status icon is only visible on the CODEX page.

Use **System > Clear CODEX pairing** to remove saved bonds when pairing a different client.

## System menu

The System menu contains Offline Mode, Factory Reset, About, Clear CODEX Pairing, Language, Settings Mode, OTA, and Network Diagnostics. Network Diagnostics checks the Open-Meteo public endpoints together with Wi-Fi, DNS, NTP, Daily Saying, internet access, and the OTA source.

## Upgrading

On first startup after this migration, obsolete weather credential and legacy CODEX feature-switch values are removed from NVS. Wi-Fi networks, weather city, page visibility/order, language, alarm, and other settings remain intact; a factory reset is not required.

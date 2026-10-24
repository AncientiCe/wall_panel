# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-10-24

First release, for the Guition ESP32-S3-4848S040 (4" 480x480 ST7701S touch panel).

### Added

- Startup screen with the logo and loading progress; it switches to Home only once the time, weather and calendar have loaded.
- Wi-Fi setup on the touch screen (scan, pick, on-screen keyboard, "Other network..." for hidden networks), saved on the device and rejoined after every reboot.
- Automatic location and time zone from the public IP address.
- Home page with a large clock, current weather and the next 5 calendar events of the week (bin collections get a bin icon).
- Calendar page with Day, Week and Month views, weeks starting on Monday, and an event count on each day.
- Google Calendar through a Google Apps Script feed in your own account (`tools/calendar-feed.gs`), protected by a token.
- Weather page from Open-Meteo: current conditions, humidity, wind, sunrise and sunset, 12-hour and 5-day forecasts, refreshed every 5 minutes, with a rain-in-X-minutes warning.
- Horoscope page with today's reading for the signs you choose.
- Analog clock page with the date and current weather.
- Idle logo screen after 90 seconds without a touch during the day; only a tap or a notification wakes it.
- Notifications for events starting within 10 minutes, rain within 30 minutes, and bin collections.
- Night mode from sunset or 20:30 (whichever is earlier) until 06:30: the screen turns off, and a tap wakes it dimmed.
- Settings web page on the panel for the calendar feed URL, horoscope signs, scene URL and night mode times.
- Fonts with German, Romanian and other Latin accented letters.
- Serial debug commands and `tools/screenshot.py` for capturing the screen over USB.

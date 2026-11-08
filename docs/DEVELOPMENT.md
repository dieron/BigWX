# Developing BigWX

## Build

```bash
pio run                      # build
pio run -t upload            # flash over USB (add --upload-port if needed)
pio device monitor           # serial log, 115200 baud
pio run -t erase             # wipe the flash, including settings and WiFi
```

- Platform `espressif32@6.9.0`, i.e. Arduino-ESP32 **2.0.17**. TFT_eSPI 2.5.x does not support the 3.x core.
- The TFT_eSPI setup for the T-Display S3 (`Setup206_LilyGo_T_Display_S3.h`, 8-bit parallel ST7789) is
  force-included from `platformio.ini`, so the library needs no edits.
- Partition table `default_16MB.csv`: two 6.25 MB app slots (for updates over WiFi), NVS for settings.

## Code layout

| File | Does |
|---|---|
| `src/main.cpp` | Start-up, WiFi (WiFiManager portal, retrying a saved network), buttons, page switching, draw loop |
| `src/settings.*` | `Settings` struct, defaults, JSON (de)serialisation, NVS storage, unit conversion helpers |
| `src/weather.*` | Background FreeRTOS task that fetches Open-Meteo / NOAA / IP location data into `WxData` |
| `src/ui.*` | All drawing: themes, fonts, drawn weather icons, the nine pages, start and WiFi screens |
| `src/webui.*` | Web server: settings page (HTML in `PAGE`), JSON API, screenshots, firmware upload |
| `src/version.h` | Version string |

Design rules:

- **Weather data stays metric** (°C, km/h, hPa, m) in `WxData`; `ui.cpp` converts with `toTemp()`, `toWind()`,
  `toPres()`, `toLen()` when drawing. Changing units never needs a new download.
- **Times are UTC epoch seconds**; local time is `t + WxData::utcOffset` (from Open-Meteo `timezone=auto`, so
  daylight saving time is handled). Tide times are stored as local epoch seconds.
- The fetch task works on a private copy of `WxData` and swaps it in under a mutex; the UI draws from a snapshot.
- Everything is drawn into one 320 × 170 16-bit sprite in PSRAM and pushed in one go (no flicker).
- **Large print**: the smallest font is FreeSansBold 12 pt. Use 18 pt / 24 pt / font 8 (72 px digits) for values.
  `bigDigits()` draws font 6/8 three times, 1 px apart, to make the strokes bold.

## Adding a page

1. Add an id to `PageId` in `settings.h` (before `PG_COUNT`) and its key and name to `PAGE_KEYS` / `PAGE_NAMES`
   in `settings.cpp`. Add it to the default `order` and `shown` arrays.
2. Write `static void pageXxx(const WxData &d)` in `ui.cpp` (call `header()` first, use `noData()` while
   `!d.ok`), and add it to the `switch` in `uiDraw()`.
3. If it needs new data, add fields to `WxData` and fetch them in `weather.cpp`.
4. Options: add fields to `Settings`, handle them in `settingsToJson()` / `settingsFromJson()`, and add inputs to
   the settings page in `webui.cpp` (`fill()` and `collect()` in the script).

Existing settings saved by an older version keep working: missing keys keep their defaults, and new pages are
appended at the end of the user's order.

## Screenshots

`GET /shot.bmp?page=<id>&night=0|1&demo=1` renders any page (also hidden ones) and returns a BMP.
`?splash=1..4` renders the start and WiFi setup screens. The pictures in `docs/images` were made this way,
with `demo=1` so no real WiFi name or IP address appears, and with a sample location.

## Releasing

1. Update `BIGWX_VERSION` in `src/version.h` and `CHANGELOG.md`.
2. `pio run` and attach `.pio/build/t-display-s3/firmware.bin` to the GitHub release, so people can update over WiFi
   from the settings page without building.

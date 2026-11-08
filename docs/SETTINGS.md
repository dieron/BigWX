# BigWX settings

All settings are made on the display's web page: open the address shown on the **Status** page
(for example `http://192.168.1.50/`) or `http://bigwx.local/` in a browser on the same network.
Change what you need and press **Save settings**. Changes apply within a few seconds; the network name
applies after a restart.

**No API keys are needed.** Every data source BigWX uses is free and open (see the README).

## Location

| Setting | What it does |
|---|---|
| Search for a place | Type a town and press *Search*, then pick a result. Fills in the name and coordinates. Uses the Open-Meteo geocoding service from your browser. |
| Name shown on the display | Shown in the title of the main page. Up to 28 characters. |
| Latitude / Longitude | Decimal degrees, e.g. `51.5074` and `-0.1278`. South and west are negative. |

Leave latitude and longitude **empty** and save to let the display find its location from your internet address
(city level, via ipapi.co or ip-api.com). This also happens on the first start when no location was entered in
the WiFi setup. When the location is found this way, the units are set to suit the country; you can change them afterwards.

## Units

| Setting | Choices |
|---|---|
| Temperature | °C, °F |
| Wind | km/h, mph, m/s, knots |
| Pressure | hPa, inHg, mmHg |
| Waves | metres, feet |
| Clock | 24 hour, 12 hour (AM/PM) |

## Pages

Tick the pages to show and use the arrows to change the order. The **first** page is the main page: the display
returns to it after a while without a button press. At least one page must be shown.

| Page | Shows | Options |
|---|---|---|
| Main | Current temperature and condition, today's high and low, the temperature for the next three time steps | *Forecast rows every* 1, 2, 3, 4 or 6 hours. *Under the temperature*: the condition or the "feels like" temperature. |
| Next hours | Temperature curve with the high and low marked, rain-chance bars, time marks (the day name at midnight) | *Time span* 12, 24, 36 or 48 hours. *Rain chance bars* on/off. |
| Next days | Day name, icon, high and low | *Number of days* 3 (larger) or 4. *Start with today*. |
| Wind | Compass arrow (points the way the wind blows; the dot is where it comes from), speed, gusts, direction | — |
| Comfort & air | Feels like, humidity, UV index (today's peak at night), US air quality index | — |
| Pressure | Pressure, 3-hour trend (steady / rising / falling / fast), the next 24 hours as a line | — |
| Sun & moon | Sunrise and sunset (tomorrow's after dark), moon phase | — |
| Ocean | Water temperature, wave height and period, the next three high and low tides | See below. Off by default. |
| Status | Settings page address, WiFi network and signal, time of the last update | — |

Category words on the Comfort & air page are short so they fit:

| UV index | 0–2 Low · 3–5 Medium · 6–7 High · 8–10 Very high · 11+ Extreme |
|---|---|
| **US AQI** | 0–50 Good · 51–100 Fair · 101–150 Poor · 151–200 Bad · 201–300 Very bad · 301+ Hazard |

### Ocean

| Setting | What it does |
|---|---|
| Use a different place for the sea | Use this when you live inland, or the nearest beach is far from the weather location. Enter the beach's coordinates. Otherwise the main location is used. |
| Tides from: Open-Meteo sea level model | Works for any coast in the world. Highs and lows are calculated from the hourly modelled sea level, so times can be off by about half an hour. |
| Tides from: NOAA tide station | Exact predictions for United States stations. Enter the station id or press *Find the nearest station* (uses the beach coordinates, or the main location). Station ids are also listed at [tidesandcurrents.noaa.gov](https://tidesandcurrents.noaa.gov/). |

If waves or water temperature show `--`, the point is probably on land: move the beach coordinates a little out to sea.

## Display

| Setting | What it does |
|---|---|
| Colours | *Automatic* switches to night colours at sunset and back at sunrise. Or always day / always night. The **KEY** button (hold 1 s) cycles through the three. |
| Night colour | Amber, red (darkest, best for a bedroom) or grey. |
| Colour values by level | In day colours, temperatures, UV, air quality and wind are coloured by level (blue = cold … red = hot). Turn off for plain white text. |
| Day / night brightness | Backlight level, 10–255 by day and 5–255 at night. |
| Turn the screen upside down | For mounting the board with the USB socket on the other side. The buttons swap sides physically. |
| Change page every | Shows the pages in turn, e.g. every 15 seconds. 0 = off. Pressing a button pauses it for 5 seconds. |
| Back to the main page after | Seconds without a button press before going back to the first page. 0 = never. Ignored when pages change automatically. |

## Device

| Setting | What it does |
|---|---|
| Update the forecast every | 5–60 minutes (default 10). Air quality and the ocean are updated every 30 minutes, NOAA tides every 3 hours. |
| Network name | The display's name on the network: `http://<name>.local/`. Letters, digits and `-`. Applies after a restart. |
| Settings password | Protects the settings page and the API with HTTP basic authentication (user name `admin`). Leave empty to keep the current password; tick *Remove the password* to remove it. It is not shown in backups. |
| Update weather now | Fetch everything again immediately. |
| Restart | Restart the display. |
| WiFi setup | Opens the WiFi setup on the display (same as holding BOOT for 3 s). The settings page stops working until the display is connected again. |
| Reset settings | Back to the defaults. WiFi stays connected. The location is then found from the internet address again. |

If you forget the password, erase the board and flash it again over USB (`pio run -t erase`, then
`pio run -t upload`). This clears all settings and the saved WiFi.

## Backup and firmware

- **Download settings backup** saves `bigwx-settings.json`. **Restore** loads such a file (also from another display).
- **Firmware update**: choose a `firmware.bin` built with PlatformIO (`.pio/build/t-display-s3/firmware.bin`) and press
  *Install firmware*. The display shows "Installing update", then restarts. Settings and WiFi are kept.

### Backup file format

```json
{
  "version": 1,
  "location": { "name": "San Diego", "lat": 32.7157, "lon": -117.1611, "located": true },
  "units": { "temp": "F", "wind": "mph", "pressure": "inHg", "length": "ft", "clock24": false },
  "pages": [ { "id": "now", "on": true }, { "id": "hours", "on": true }, { "id": "days", "on": true },
             { "id": "wind", "on": true }, { "id": "air", "on": true }, { "id": "pressure", "on": true },
             { "id": "sun", "on": true }, { "id": "ocean", "on": false }, { "id": "status", "on": true } ],
  "main": { "step": 3, "line2": "condition" },
  "hours": { "span": 24, "rain": true },
  "days": { "count": 4, "today": false },
  "ocean": { "custom": false, "lat": 0, "lon": 0, "tide": "model", "station": "" },
  "display": { "mode": "auto", "day": 220, "night": 40, "palette": "amber", "colours": true,
               "flip": false, "cycle": 0, "back": 60 },
  "device": { "update": 10, "host": "bigwx" }
}
```

Every part is optional when restoring: a file with only `{"units": {"temp": "C"}}` changes just that.

| Key | Values |
|---|---|
| `units.temp` | `C`, `F` |
| `units.wind` | `kmh`, `mph`, `ms`, `kn` |
| `units.pressure` | `hPa`, `inHg`, `mmHg` |
| `units.length` | `m`, `ft` |
| `pages[].id` | `now`, `hours`, `days`, `wind`, `air`, `pressure`, `sun`, `ocean`, `status` |
| `main.step` | 1–6 (hours) |
| `main.line2` | `condition`, `feels` |
| `hours.span` | 12, 24, 36, 48 |
| `days.count` | 3, 4 |
| `ocean.tide` | `model`, `noaa` |
| `display.mode` | `auto`, `day`, `night` |
| `display.palette` | `amber`, `red`, `grey` |
| `display.day` / `display.night` | 10–255 / 5–255 |
| `display.cycle` / `display.back` | 0–3600 (seconds) |
| `device.update` | 5–60 (minutes) |
| `device.password` | Only when restoring or setting it; never included in a download. |

## Web API

The settings page uses this API; you can use it from scripts too (add `-u admin:<password>` if a password is set).

| Request | Does |
|---|---|
| `GET /api/config` | Current settings as JSON. `?download=1` returns the backup file. |
| `POST /api/config` | Change settings. Body: JSON as above, any subset. Returns the new settings. |
| `GET /api/status` | Firmware version, uptime, IP, WiFi name and signal, free memory, location, current temperature, last update. |
| `POST /api/action?do=refresh` | Fetch the weather now. Also `reboot`, `wifi` (open WiFi setup), `reset` (default settings). |
| `GET /shot.bmp?page=<id>` | Screenshot of a page as a BMP image. `&night=0` or `1` forces the colours; `&demo=1` hides the WiFi name and IP address. |
| `POST /update` | Firmware update (multipart form upload of `firmware.bin`). |

Example:

```bash
curl -X POST -H "Content-Type: application/json" -d '{"display":{"mode":"night"}}' http://bigwx.local/api/config
curl -o main.bmp "http://bigwx.local/shot.bmp?page=now"
```

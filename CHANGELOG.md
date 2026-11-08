# Changelog

All notable changes to BigWX. Versions follow [Semantic Versioning](https://semver.org/).

## [1.1.0] - 2026-11-07

### Added
- Advanced web settings page: show, hide and reorder pages; options per page (main page forecast step and second
  line, 12–48 hour graph, 3 or 4 days, starting today); live screen preview; settings backup and restore;
  firmware update over WiFi; optional password; network name.
- Separate units for temperature, wind (km/h, mph, m/s, knots), pressure (hPa, inHg, mmHg) and waves (m, ft).
- Works anywhere: finds the location from the internet address on the first start and picks units for the country;
  worldwide tides from the Open-Meteo sea level model (NOAA stations stay available for exact US tides);
  moon phase mirrored for the southern hemisphere.
- Night colour choice (amber, red, grey), plain white values option, screen flip, adjustable update interval.
- JSON API (`/api/config`, `/api/status`, `/api/action`) and `/shot.bmp` screenshots.
- Documentation: settings guide, development guide.

### Changed
- Settings are stored as one JSON document; settings from 1.0 are migrated automatically.
- Weather data is kept in metric and converted when drawn, so changing units is instant.
- Shorter category words on the Comfort & air page (UV "Medium", AQI "Fair/Poor/Bad") so they fit.
- No default location or COM port in the code.

### Fixed
- Tide times with AM/PM no longer run off the right edge.

## [1.0.0] - 2026-11-07

First version.

- Nine large-print pages: main (current weather and the next hours), next 24 hours, next 4 days, wind,
  comfort and air, pressure, sun and moon, ocean, status.
- Day colours and an amber night mode that switch at sunset and sunrise.
- Open-Meteo forecast, air quality and marine data; NOAA tides.
- WiFi setup from a phone (WiFiManager); a saved network is retried instead of opening the setup portal.
- Simple web settings page with place search and nearest tide station lookup.

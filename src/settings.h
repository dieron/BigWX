#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ---- choices -------------------------------------------------------------
enum TempUnit : uint8_t { TEMP_C, TEMP_F };
enum WindUnit : uint8_t { WIND_KMH, WIND_MPH, WIND_MS, WIND_KN };
enum PresUnit : uint8_t { PRES_HPA, PRES_INHG, PRES_MMHG };
enum LenUnit  : uint8_t { LEN_M, LEN_FT };
enum DisplayMode : uint8_t { MODE_AUTO, MODE_DAY, MODE_NIGHT };
enum NightPalette : uint8_t { NIGHT_AMBER, NIGHT_RED, NIGHT_GREY };
enum TideSource : uint8_t { TIDE_MODEL, TIDE_NOAA };
enum MainLine2 : uint8_t { LINE2_CONDITION, LINE2_FEELS };

// Page ids. The order here is only the default order; the user can reorder and hide pages.
enum PageId : uint8_t { PG_NOW, PG_HOURS, PG_DAYS, PG_WIND, PG_AIR, PG_PRESSURE, PG_SUN, PG_OCEAN, PG_INFO, PG_COUNT };
extern const char *const PAGE_KEYS[PG_COUNT];   // "now", "hours", ...
extern const char *const PAGE_NAMES[PG_COUNT];  // "Main", "Next hours", ...

struct Settings {
  // Location. located = false until the user sets one or it is found from the IP address.
  String name = "";
  float lat = 0, lon = 0;
  bool located = false;

  // Units
  uint8_t tempUnit = TEMP_C, windUnit = WIND_KMH, presUnit = PRES_HPA, lenUnit = LEN_M;
  bool clock24 = true;

  // Pages: display order and which are shown
  uint8_t order[PG_COUNT] = {PG_NOW, PG_HOURS, PG_DAYS, PG_WIND, PG_AIR, PG_PRESSURE, PG_SUN, PG_OCEAN, PG_INFO};
  bool shown[PG_COUNT] = {true, true, true, true, true, true, true, false, true};

  // Page options
  uint8_t mainStep = 3;          // hours between the forecast rows on the main page (1..6)
  uint8_t mainLine2 = LINE2_CONDITION;
  uint8_t hoursSpan = 24;        // 12, 24, 36 or 48
  bool hoursRain = true;         // rain-chance bars
  uint8_t daysCount = 4;         // 3 or 4
  bool daysToday = false;        // start with today instead of tomorrow

  // Ocean page
  bool coastSet = false;         // false: use the main location for the sea data
  float coastLat = 0, coastLon = 0;
  uint8_t tideSource = TIDE_MODEL;
  String tideStation = "";       // NOAA CO-OPS station id (US only)

  // Display
  uint8_t mode = MODE_AUTO;
  uint8_t dayBright = 220, nightBright = 40;
  uint8_t nightPalette = NIGHT_AMBER;
  bool colours = true;           // colour values by level in day mode (off = plain white)
  bool flip = false;             // rotate the screen 180 degrees
  uint16_t autoCycle = 0;        // seconds per page, 0 = off
  uint16_t backToMain = 60;      // seconds idle before returning to the first page, 0 = never

  // Data / device
  uint8_t updateMin = 10;        // forecast refresh, minutes
  String hostname = "bigwx";
  String webPassword = "";       // empty = settings page open on the local network
};

extern Settings cfg;

void loadSettings();
void saveSettings();
void resetSettings();                       // back to defaults (keeps WiFi)
void settingsToJson(JsonDocument &doc, bool includeSecrets = false);
bool settingsFromJson(JsonVariantConst v, String &err);  // partial updates allowed
void setUnitsForCountry(const String &countryCode);      // US-style or metric defaults

// ---- unit helpers (all data is kept in metric) ----------------------------
float toTemp(float c);
float toWind(float kmh);
float toPres(float hpa);
float toLen(float m);
const char *windUnitName();
const char *presUnitName();
const char *lenUnitName();
int presDecimals();

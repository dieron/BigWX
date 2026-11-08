// Settings are stored as one JSON document in NVS (namespace "bigwx", key "cfg").
// The same JSON is used by the web settings page and for backup / restore.

#include "settings.h"
#include <Preferences.h>

Settings cfg;

const char *const PAGE_KEYS[PG_COUNT] = {"now", "hours", "days", "wind", "air", "pressure", "sun", "ocean", "status"};
const char *const PAGE_NAMES[PG_COUNT] = {"Main", "Next hours", "Next days", "Wind", "Comfort & air",
                                          "Pressure", "Sun & moon", "Ocean", "Status"};

static const char *const TEMP_K[] = {"C", "F"};
static const char *const WIND_K[] = {"kmh", "mph", "ms", "kn"};
static const char *const PRES_K[] = {"hPa", "inHg", "mmHg"};
static const char *const LEN_K[] = {"m", "ft"};
static const char *const MODE_K[] = {"auto", "day", "night"};
static const char *const PAL_K[] = {"amber", "red", "grey"};
static const char *const TIDE_K[] = {"model", "noaa"};
static const char *const LINE2_K[] = {"condition", "feels"};

template <size_t N>
static void readEnum(JsonVariantConst v, const char *const (&keys)[N], uint8_t &out) {
  if (!v.is<const char *>()) return;
  for (size_t i = 0; i < N; i++)
    if (strcmp(v.as<const char *>(), keys[i]) == 0) { out = i; return; }
}

template <typename T>
static void readNum(JsonVariantConst v, T &out, T lo, T hi) {
  if (v.is<float>() || v.is<int>()) out = (T)constrain(v.as<float>(), (float)lo, (float)hi);
}

static void readBool(JsonVariantConst v, bool &out) {
  if (v.is<bool>()) out = v.as<bool>();
}

static void readStr(JsonVariantConst v, String &out, size_t maxLen) {
  if (v.is<const char *>()) {
    out = v.as<const char *>();
    out.trim();
    if (out.length() > maxLen) out = out.substring(0, maxLen);
  }
}

void settingsToJson(JsonDocument &doc, bool includeSecrets) {
  const Settings &c = cfg;
  doc["version"] = 1;
  JsonObject loc = doc["location"].to<JsonObject>();
  loc["name"] = c.name;
  loc["lat"] = serialized(String(c.lat, 4));
  loc["lon"] = serialized(String(c.lon, 4));
  loc["located"] = c.located;

  JsonObject u = doc["units"].to<JsonObject>();
  u["temp"] = TEMP_K[c.tempUnit];
  u["wind"] = WIND_K[c.windUnit];
  u["pressure"] = PRES_K[c.presUnit];
  u["length"] = LEN_K[c.lenUnit];
  u["clock24"] = c.clock24;

  JsonArray pages = doc["pages"].to<JsonArray>();
  for (int i = 0; i < PG_COUNT; i++) {
    JsonObject p = pages.add<JsonObject>();
    p["id"] = PAGE_KEYS[c.order[i]];
    p["name"] = PAGE_NAMES[c.order[i]];
    p["on"] = c.shown[c.order[i]];
  }

  JsonObject m = doc["main"].to<JsonObject>();
  m["step"] = c.mainStep;
  m["line2"] = LINE2_K[c.mainLine2];
  JsonObject h = doc["hours"].to<JsonObject>();
  h["span"] = c.hoursSpan;
  h["rain"] = c.hoursRain;
  JsonObject d = doc["days"].to<JsonObject>();
  d["count"] = c.daysCount;
  d["today"] = c.daysToday;

  JsonObject o = doc["ocean"].to<JsonObject>();
  o["custom"] = c.coastSet;
  o["lat"] = serialized(String(c.coastLat, 4));
  o["lon"] = serialized(String(c.coastLon, 4));
  o["tide"] = TIDE_K[c.tideSource];
  o["station"] = c.tideStation;

  JsonObject s = doc["display"].to<JsonObject>();
  s["mode"] = MODE_K[c.mode];
  s["day"] = c.dayBright;
  s["night"] = c.nightBright;
  s["palette"] = PAL_K[c.nightPalette];
  s["colours"] = c.colours;
  s["flip"] = c.flip;
  s["cycle"] = c.autoCycle;
  s["back"] = c.backToMain;

  JsonObject dv = doc["device"].to<JsonObject>();
  dv["update"] = c.updateMin;
  dv["host"] = c.hostname;
  dv["hasPassword"] = c.webPassword.length() > 0;
  if (includeSecrets) dv["password"] = c.webPassword;
}

bool settingsFromJson(JsonVariantConst v, String &err) {
  if (!v.is<JsonObjectConst>()) { err = "not a JSON object"; return false; }
  Settings &c = cfg;

  JsonVariantConst loc = v["location"];
  if (!loc.isNull()) {
    readStr(loc["name"], c.name, 28);
    readNum(loc["lat"], c.lat, -90.0f, 90.0f);
    readNum(loc["lon"], c.lon, -180.0f, 180.0f);
    if (!loc["lat"].isNull() || !loc["lon"].isNull()) c.located = true;
    readBool(loc["located"], c.located);
  }

  JsonVariantConst u = v["units"];
  readEnum(u["temp"], TEMP_K, c.tempUnit);
  readEnum(u["wind"], WIND_K, c.windUnit);
  readEnum(u["pressure"], PRES_K, c.presUnit);
  readEnum(u["length"], LEN_K, c.lenUnit);
  readBool(u["clock24"], c.clock24);

  JsonArrayConst pages = v["pages"];
  if (!pages.isNull()) {
    // Accept any order; pages missing from the list keep their place at the end.
    uint8_t order[PG_COUNT];
    bool used[PG_COUNT] = {};
    int n = 0;
    for (JsonVariantConst p : pages) {
      const char *id = p["id"] | "";
      for (int k = 0; k < PG_COUNT; k++) {
        if (!used[k] && strcmp(id, PAGE_KEYS[k]) == 0) {
          used[k] = true;
          order[n++] = k;
          readBool(p["on"], c.shown[k]);
        }
      }
    }
    for (int i = 0; i < PG_COUNT; i++)
      if (!used[c.order[i]]) order[n++] = c.order[i];
    memcpy(c.order, order, sizeof(order));
    bool any = false;
    for (int k = 0; k < PG_COUNT; k++) any |= c.shown[k];
    if (!any) c.shown[PG_NOW] = true;
  }

  readNum(v["main"]["step"], c.mainStep, (uint8_t)1, (uint8_t)6);
  readEnum(v["main"]["line2"], LINE2_K, c.mainLine2);
  if (!v["hours"]["span"].isNull()) {
    int s = v["hours"]["span"].as<int>();
    c.hoursSpan = s <= 12 ? 12 : s <= 24 ? 24 : s <= 36 ? 36 : 48;
  }
  readBool(v["hours"]["rain"], c.hoursRain);
  readNum(v["days"]["count"], c.daysCount, (uint8_t)3, (uint8_t)4);
  readBool(v["days"]["today"], c.daysToday);

  JsonVariantConst o = v["ocean"];
  readBool(o["custom"], c.coastSet);
  readNum(o["lat"], c.coastLat, -90.0f, 90.0f);
  readNum(o["lon"], c.coastLon, -180.0f, 180.0f);
  readEnum(o["tide"], TIDE_K, c.tideSource);
  readStr(o["station"], c.tideStation, 12);

  JsonVariantConst s = v["display"];
  readEnum(s["mode"], MODE_K, c.mode);
  readNum(s["day"], c.dayBright, (uint8_t)10, (uint8_t)255);
  readNum(s["night"], c.nightBright, (uint8_t)5, (uint8_t)255);
  readEnum(s["palette"], PAL_K, c.nightPalette);
  readBool(s["colours"], c.colours);
  readBool(s["flip"], c.flip);
  readNum(s["cycle"], c.autoCycle, (uint16_t)0, (uint16_t)3600);
  readNum(s["back"], c.backToMain, (uint16_t)0, (uint16_t)3600);

  JsonVariantConst dv = v["device"];
  readNum(dv["update"], c.updateMin, (uint8_t)5, (uint8_t)60);
  if (dv["host"].is<const char *>()) {
    String h;
    readStr(dv["host"], h, 24);
    String clean;
    for (char ch : h) if (isalnum(ch) || ch == '-') clean += (char)tolower(ch);
    if (clean.length()) c.hostname = clean;
  }
  if (dv["password"].is<const char *>()) readStr(dv["password"], c.webPassword, 32);
  return true;
}

void setUnitsForCountry(const String &cc) {
  bool us = cc == "US" || cc == "LR" || cc == "MM" || cc == "PR" || cc == "GU";
  cfg.tempUnit = us ? TEMP_F : TEMP_C;
  cfg.windUnit = us ? WIND_MPH : (cc == "GB" ? WIND_MPH : WIND_KMH);
  cfg.presUnit = us ? PRES_INHG : PRES_HPA;
  cfg.lenUnit = us ? LEN_FT : LEN_M;
  cfg.clock24 = !(us || cc == "CA" || cc == "AU" || cc == "NZ" || cc == "IN" || cc == "PH");
}

// ---------------------------------------------------------------- storage

static Preferences prefs;

// Settings saved by version 1.0 (one NVS key per value).
static void loadLegacy() {
  if (!prefs.isKey("lat")) return;
  cfg.name = prefs.getString("name", "");
  cfg.lat = prefs.getFloat("lat", 0);
  cfg.lon = prefs.getFloat("lon", 0);
  cfg.located = true;
  if (!prefs.getBool("metric", false)) setUnitsForCountry("US");
  cfg.clock24 = prefs.getBool("clock24", cfg.clock24);
  cfg.shown[PG_OCEAN] = prefs.getBool("coast", false);
  cfg.coastSet = prefs.isKey("clat");
  cfg.coastLat = prefs.getFloat("clat", 0);
  cfg.coastLon = prefs.getFloat("clon", 0);
  cfg.tideStation = prefs.getString("tide", "");
  cfg.tideSource = cfg.tideStation.length() ? TIDE_NOAA : TIDE_MODEL;
  cfg.mode = prefs.getUChar("mode", MODE_AUTO);
  cfg.dayBright = prefs.getUChar("bday", 220);
  cfg.nightBright = prefs.getUChar("bnight", 40);
  cfg.autoCycle = prefs.getUShort("cycle", 0);
  cfg.backToMain = prefs.getUShort("back", 60);
  Serial.println("[cfg] migrated settings from version 1.0");
}

void loadSettings() {
  cfg = Settings();
  if (!prefs.begin("bigwx", false)) return;
  String js = prefs.isKey("cfg") ? prefs.getString("cfg", "") : String();
  if (js.length()) {
    JsonDocument doc;
    if (!deserializeJson(doc, js)) {
      String err;
      settingsFromJson(doc.as<JsonVariantConst>(), err);
    }
  } else {
    loadLegacy();
  }
  prefs.end();
  if (js.length() == 0 && cfg.located) saveSettings();  // finish the migration
}

void saveSettings() {
  JsonDocument doc;
  settingsToJson(doc, true);
  String js;
  serializeJson(doc, js);
  prefs.begin("bigwx", false);
  prefs.clear();  // also drops the old one-key-per-value settings
  prefs.putString("cfg", js);
  prefs.end();
}

void resetSettings() {
  cfg = Settings();
  prefs.begin("bigwx", false);
  prefs.clear();
  prefs.end();
}

// ---------------------------------------------------------------- units

float toTemp(float c) { return cfg.tempUnit == TEMP_F ? c * 9.0f / 5.0f + 32.0f : c; }

float toWind(float kmh) {
  switch (cfg.windUnit) {
    case WIND_MPH: return kmh / 1.609344f;
    case WIND_MS: return kmh / 3.6f;
    case WIND_KN: return kmh / 1.852f;
    default: return kmh;
  }
}

float toPres(float hpa) {
  switch (cfg.presUnit) {
    case PRES_INHG: return hpa * 0.0295300f;
    case PRES_MMHG: return hpa * 0.750062f;
    default: return hpa;
  }
}

float toLen(float m) { return cfg.lenUnit == LEN_FT ? m * 3.28084f : m; }

const char *windUnitName() {
  static const char *n[] = {"km/h", "mph", "m/s", "kn"};
  return n[cfg.windUnit];
}
const char *presUnitName() {
  static const char *n[] = {"hPa", "inHg", "mmHg"};
  return n[cfg.presUnit];
}
const char *lenUnitName() { return cfg.lenUnit == LEN_FT ? "ft" : "m"; }
int presDecimals() { return cfg.presUnit == PRES_INHG ? 2 : 0; }

// Background data fetching. Sources (all free, no API key):
//   Open-Meteo forecast, air quality and marine APIs  https://open-meteo.com  (CC BY 4.0)
//   NOAA CO-OPS tide predictions (US stations)         https://tidesandcurrents.noaa.gov
//   ipapi.co / ip-api.com, only once, to guess the location when none is set

#include "weather.h"
#include "settings.h"
#include "version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static WxData wx;
static SemaphoreHandle_t lock;
static volatile bool forceRefresh = true;

static const uint32_t AQ_EVERY    = 30UL * 60 * 1000;
static const uint32_t SEA_EVERY   = 30UL * 60 * 1000;
static const uint32_t TIDE_EVERY  = 3UL * 3600 * 1000;
static const uint32_t RETRY_EVERY = 60UL * 1000;

// ---------------------------------------------------------------------------

static bool getJson(const String &url, JsonDocument &doc, char *err, size_t errLen) {
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = url.startsWith("https:");
  if (https) secure.setInsecure();  // public, read-only data; no certificate bundle needed
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed from the stream
  http.setTimeout(15000);
  http.setConnectTimeout(10000);
  bool ok = https ? http.begin(secure, url) : http.begin(plain, url);
  if (!ok) { snprintf(err, errLen, "bad URL"); return false; }
  http.setUserAgent("BigWX/" BIGWX_VERSION " (ESP32 weather display)");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err, errLen, code < 0 ? "No connection (%d)" : "Server error %d", code);
    http.end();
    return false;
  }
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e) { snprintf(err, errLen, "Bad data (%s)", e.c_str()); return false; }
  return true;
}

static float f(JsonVariantConst v) { return v.isNull() ? NAN : v.as<float>(); }
static int   n(JsonVariantConst v, int dflt = -1) { return v.isNull() ? dflt : (int)lroundf(v.as<float>()); }

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + (long)doe - 719468L;
}

// ---------------------------------------------------------------------------

// Guess the location from the public IP address (city level). Used once, when no location is set.
static bool geolocate() {
  JsonDocument doc;
  char err[64];
  String city, cc;
  float lat = NAN, lon = NAN;
  if (getJson("https://ipapi.co/json/", doc, err, sizeof(err)) && !doc["latitude"].isNull()) {
    city = doc["city"] | "";
    cc = doc["country_code"] | "";
    lat = doc["latitude"];
    lon = doc["longitude"];
  } else if (getJson("http://ip-api.com/json/?fields=status,countryCode,city,lat,lon", doc, err, sizeof(err)) &&
             strcmp(doc["status"] | "", "success") == 0) {
    city = doc["city"] | "";
    cc = doc["countryCode"] | "";
    lat = doc["lat"];
    lon = doc["lon"];
  }
  if (isnan(lat) || isnan(lon)) return false;
  cfg.name = city.substring(0, 28);
  cfg.lat = lat;
  cfg.lon = lon;
  cfg.located = true;
  setUnitsForCountry(cc);
  saveSettings();
  Serial.printf("[wx] location from IP: %s (%s) %.3f, %.3f\n", city.c_str(), cc.c_str(), lat, lon);
  return true;
}

static bool fetchForecast(WxData &d) {
  char url[900];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,"
    "cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index"
    "&hourly=temperature_2m,precipitation_probability,weather_code,pressure_msl,is_day"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
    "precipitation_sum,sunrise,sunset,uv_index_max,wind_speed_10m_max"
    "&timezone=auto&timeformat=unixtime&past_hours=3&forecast_hours=49&forecast_days=6",
    cfg.lat, cfg.lon);

  JsonDocument doc;
  if (!getJson(url, doc, d.err, sizeof(d.err))) return false;

  d.utcOffset = doc["utc_offset_seconds"] | 0;
  JsonObjectConst c = doc["current"];
  d.now.t      = c["time"] | 0;
  d.now.temp   = f(c["temperature_2m"]);
  d.now.feels  = f(c["apparent_temperature"]);
  d.now.hum    = n(c["relative_humidity_2m"]);
  d.now.isDay  = (c["is_day"] | 1) == 1;
  d.now.precip = f(c["precipitation"]);
  d.now.code   = n(c["weather_code"]);
  d.now.cloud  = n(c["cloud_cover"]);
  d.now.pres   = f(c["pressure_msl"]);
  d.now.wind   = f(c["wind_speed_10m"]);
  d.now.dir    = n(c["wind_direction_10m"]);
  d.now.gust   = f(c["wind_gusts_10m"]);
  d.now.uv     = f(c["uv_index"]);

  JsonObjectConst h = doc["hourly"];
  JsonArrayConst ht = h["time"];
  d.nHours = 0;
  d.nowIdx = 0;
  for (size_t i = 0; i < ht.size() && d.nHours < WxData::MAXH; i++) {
    HourPt &p = d.hours[d.nHours++];
    p.t     = ht[i].as<long>();
    p.temp  = f(h["temperature_2m"][i]);
    p.pop   = n(h["precipitation_probability"][i], 0);
    p.code  = n(h["weather_code"][i], 0);
    p.pres  = f(h["pressure_msl"][i]);
    p.isDay = (h["is_day"][i] | 1) == 1;
    if (p.t <= d.now.t) d.nowIdx = d.nHours - 1;
  }
  d.presTrend = NAN;
  if (d.nowIdx >= 3 && !isnan(d.hours[d.nowIdx].pres) && !isnan(d.hours[d.nowIdx - 3].pres))
    d.presTrend = d.hours[d.nowIdx].pres - d.hours[d.nowIdx - 3].pres;

  JsonObjectConst dy = doc["daily"];
  JsonArrayConst dt = dy["time"];
  d.nDays = 0;
  for (size_t i = 0; i < dt.size() && d.nDays < WxData::MAXD; i++) {
    DayPt &p = d.days[d.nDays++];
    p.t       = dt[i].as<long>();
    p.code    = n(dy["weather_code"][i], 0);
    p.tmax    = f(dy["temperature_2m_max"][i]);
    p.tmin    = f(dy["temperature_2m_min"][i]);
    p.pop     = n(dy["precipitation_probability_max"][i], 0);
    p.rain    = f(dy["precipitation_sum"][i]);
    p.sunrise = dy["sunrise"][i].as<long>();
    p.sunset  = dy["sunset"][i].as<long>();
    p.uvMax   = f(dy["uv_index_max"][i]);
    p.windMax = f(dy["wind_speed_10m_max"][i]);
  }
  d.ok = d.nHours > 0 && d.nDays > 0;
  d.fetched = time(nullptr);
  if (d.ok) d.err[0] = 0;
  return d.ok;
}

static bool fetchAir(WxData &d) {
  char url[300];
  snprintf(url, sizeof(url),
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
    "&current=us_aqi,pm2_5&timezone=auto&timeformat=unixtime", cfg.lat, cfg.lon);
  JsonDocument doc;
  char err[64];
  if (!getJson(url, doc, err, sizeof(err))) return false;
  d.aqi  = n(doc["current"]["us_aqi"]);
  d.pm25 = f(doc["current"]["pm2_5"]);
  d.aqOk = d.aqi >= 0;
  return d.aqOk;
}

static float seaLat() { return cfg.coastSet ? cfg.coastLat : cfg.lat; }
static float seaLon() { return cfg.coastSet ? cfg.coastLon : cfg.lon; }

// Waves and water temperature, plus modelled tides (sea level) from the same request.
static bool fetchMarine(WxData &d, bool wantModelTides) {
  char url[400];
  snprintf(url, sizeof(url),
    "https://marine-api.open-meteo.com/v1/marine?latitude=%.4f&longitude=%.4f"
    "&current=wave_height,wave_period,wave_direction,sea_surface_temperature"
    "%s&timezone=auto&timeformat=unixtime",
    seaLat(), seaLon(), wantModelTides ? "&hourly=sea_level_height_msl&past_hours=2&forecast_hours=50" : "");
  JsonDocument doc;
  char err[64];
  if (!getJson(url, doc, err, sizeof(err))) return false;
  JsonObjectConst c = doc["current"];
  d.waveH      = f(c["wave_height"]);
  d.wavePeriod = f(c["wave_period"]);
  d.waveDir    = n(c["wave_direction"]);
  d.sst        = f(c["sea_surface_temperature"]);
  d.marineOk   = !isnan(d.waveH) || !isnan(d.sst);

  if (wantModelTides) {
    // Find highs and lows in the hourly sea level and refine each with a parabola through 3 points.
    int32_t off = doc["utc_offset_seconds"] | 0;
    JsonArrayConst t = doc["hourly"]["time"];
    JsonArrayConst h = doc["hourly"]["sea_level_height_msl"];
    d.nTides = 0;
    for (size_t i = 1; i + 1 < h.size() && d.nTides < WxData::MAXT; i++) {
      if (h[i - 1].isNull() || h[i].isNull() || h[i + 1].isNull()) continue;
      float a = h[i - 1], b = h[i], cc = h[i + 1];
      bool hi = b > a && b >= cc, lo = b < a && b <= cc;
      if (!hi && !lo) continue;
      float den = a - 2 * b + cc;
      float dx = den != 0 ? 0.5f * (a - cc) / den : 0;  // hours from the middle sample
      dx = constrain(dx, -0.5f, 0.5f);
      TidePt &tp = d.tides[d.nTides++];
      tp.local = t[i].as<long>() + (long)(dx * 3600) + off;
      tp.height = b - 0.25f * (a - cc) * dx;
      tp.high = hi;
    }
    d.tideOk = d.nTides > 0;
    d.tideModel = true;
  }
  return d.marineOk || d.tideOk;
}

static bool fetchNoaaTides(WxData &d, time_t localNowT) {
  if (cfg.tideStation.isEmpty() || localNowT == 0) return false;
  struct tm lt;
  gmtime_r(&localNowT, &lt);
  char url[400];
  snprintf(url, sizeof(url),
    "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=predictions&application=BigWX"
    "&begin_date=%04d%02d%02d&range=48&datum=MLLW&station=%s&time_zone=lst_ldt&units=metric&interval=hilo&format=json",
    lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, cfg.tideStation.c_str());
  JsonDocument doc;
  char err[64];
  if (!getJson(url, doc, err, sizeof(err))) return false;
  d.nTides = 0;
  for (JsonObjectConst e : doc["predictions"].as<JsonArrayConst>()) {
    if (d.nTides >= WxData::MAXT) break;
    const char *t = e["t"] | "";  // "2026-11-07 03:12", station local time
    int Y, M, D, hh, mm;
    if (sscanf(t, "%d-%d-%d %d:%d", &Y, &M, &D, &hh, &mm) != 5) continue;
    TidePt &tp = d.tides[d.nTides++];
    tp.local  = (time_t)daysFromCivil(Y, M, D) * 86400 + hh * 3600 + mm * 60;
    tp.height = atof(e["v"] | "0");
    tp.high   = (e["type"] | "L")[0] == 'H';
  }
  d.tideOk = d.nTides > 0;
  d.tideModel = false;
  return d.tideOk;
}

// ---------------------------------------------------------------------------

static void fetchTask(void *) {
  uint32_t lastWx = 0, lastAq = 0, lastSea = 0, lastTide = 0, lastGeo = 0;
  bool wxGood = false, aqGood = false, seaGood = false, tideGood = false;
  bool first = true;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (WiFi.status() != WL_CONNECTED) continue;

    uint32_t ms = millis();
    bool force = forceRefresh;
    forceRefresh = false;
    auto due = [&](uint32_t last, bool good, uint32_t every) {
      return force || first || (ms - last) > (good ? every : RETRY_EVERY);
    };

    WxData work;  // private copy, so the display never sees half-updated data
    xSemaphoreTake(lock, portMAX_DELAY);
    work = wx;
    xSemaphoreGive(lock);
    bool changed = false;

    if (!cfg.located) {
      if (first || force || ms - lastGeo > RETRY_EVERY) {
        lastGeo = ms;
        work.locating = true;
        xSemaphoreTake(lock, portMAX_DELAY);
        wx.locating = true;
        xSemaphoreGive(lock);
        if (!geolocate()) {
          snprintf(work.err, sizeof(work.err), "Set the location on the settings page");
          xSemaphoreTake(lock, portMAX_DELAY);
          wx = work;
          xSemaphoreGive(lock);
          first = false;
          continue;
        }
        work.locating = false;
        force = true;
      } else continue;
    }
    work.locating = false;

    bool ocean = cfg.shown[PG_OCEAN];
    bool modelTides = ocean && cfg.tideSource == TIDE_MODEL;

    if (due(lastWx, wxGood, cfg.updateMin * 60000UL)) {
      lastWx = ms;
      wxGood = fetchForecast(work);
      Serial.printf("[wx] forecast %s %s\n", wxGood ? "ok" : "FAILED", work.err);
      changed = true;
    }
    if (due(lastAq, aqGood, AQ_EVERY)) {
      lastAq = ms;
      aqGood = fetchAir(work);
      Serial.printf("[wx] air quality %s\n", aqGood ? "ok" : "failed");
      changed = true;
    }
    if (ocean && due(lastSea, seaGood, SEA_EVERY)) {
      lastSea = ms;
      seaGood = fetchMarine(work, modelTides);
      if (modelTides) tideGood = work.tideOk;
      Serial.printf("[wx] marine %s, model tides %d\n", seaGood ? "ok" : "failed", modelTides ? work.nTides : -1);
      changed = true;
    }
    if (ocean && !modelTides && work.ok && due(lastTide, tideGood, TIDE_EVERY)) {
      lastTide = ms;
      tideGood = fetchNoaaTides(work, localNow(work));
      Serial.printf("[wx] NOAA tides %s (%d)\n", tideGood ? "ok" : "failed", work.nTides);
      changed = true;
    }
    if (!ocean) { work.marineOk = false; work.tideOk = false; }
    first = false;

    if (changed) {
      xSemaphoreTake(lock, portMAX_DELAY);
      wx = work;
      xSemaphoreGive(lock);
    }
  }
}

void weatherBegin() {
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(fetchTask, "fetch", 20 * 1024, nullptr, 1, nullptr, 0);
}

void weatherRefreshNow() { forceRefresh = true; }

bool weatherSnapshot(WxData &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = wx;
  xSemaphoreGive(lock);
  return out.ok;
}

time_t localNow(const WxData &d) {
  time_t t = time(nullptr);
  if (t < 1700000000) return 0;  // NTP not synced yet
  return t + d.utcOffset;
}

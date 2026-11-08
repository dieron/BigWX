#pragma once
#include <Arduino.h>

// All values are metric (C, km/h, hPa, mm, m); the display converts them.
// All times are UTC epoch seconds unless the name says "local" (local = UTC + utcOffset).

struct HourPt { time_t t; float temp; float pres; int pop; int code; bool isDay; };
struct DayPt  { time_t t; int code; float tmax, tmin; int pop; float rain; time_t sunrise, sunset; float uvMax; float windMax; };
struct TidePt { time_t local; float height; bool high; };

struct WxNow {
  time_t t = 0;
  float temp = NAN, feels = NAN, pres = NAN, wind = NAN, gust = NAN, uv = NAN, precip = NAN;
  int hum = -1, dir = -1, code = -1, cloud = -1;
  bool isDay = true;
};

struct WxData {
  // Open-Meteo forecast
  bool ok = false;
  time_t fetched = 0;
  int32_t utcOffset = 0;
  WxNow now;
  static const int MAXH = 56;   // 3 past hours + 49 coming hours
  HourPt hours[MAXH];
  int nHours = 0;
  int nowIdx = 0;               // index in hours[] of the current hour
  static const int MAXD = 7;
  DayPt days[MAXD];
  int nDays = 0;
  float presTrend = NAN;        // hPa change over the last 3 hours

  // Air quality
  bool aqOk = false;
  int aqi = -1;
  float pm25 = NAN;

  // Ocean
  bool marineOk = false;
  float waveH = NAN, wavePeriod = NAN, sst = NAN;
  int waveDir = -1;
  bool tideOk = false;
  bool tideModel = true;        // true: Open-Meteo sea level model, false: NOAA station
  static const int MAXT = 12;
  TidePt tides[MAXT];
  int nTides = 0;

  char err[64] = "";
  bool locating = false;        // looking up the location from the IP address
};

void weatherBegin();            // start the background fetch task
void weatherRefreshNow();       // fetch everything on the next loop of the task
bool weatherSnapshot(WxData &out);  // thread-safe copy for drawing
time_t localNow(const WxData &d);   // 0 if the clock is not set yet

// BigWX display: 320 x 170 landscape, everything drawn into one PSRAM sprite and pushed at once.
// Text is never smaller than FreeSansBold 12pt (about 17 px capital height); values use 18pt, 24pt
// or the 72 px digit font. All weather data arrives in metric and is converted here.

#include "ui.h"
#include <TFT_eSPI.h>
#include <WiFi.h>

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);

#define F12 &FreeSansBold12pt7b
#define F18 &FreeSansBold18pt7b
#define F24 &FreeSansBold24pt7b
static const int CAP12 = 17, CAP18 = 25, CAP24 = 34;  // capital heights, for placing the degree ring

static const int W = 320, H = 170, HDR = 30;
static const int BL_PIN = 38, BL_CH = 0;

static String toastMsg;
static uint32_t toastUntil = 0;
static int lastBright = -1;
static int rotation = -1;
static bool demo = false;

// ---------------------------------------------------------------- theme

struct Theme {
  bool night;
  uint16_t bg, hdrBg, fg, dim, line, accent, sun, moon, cloud, darkCloud, rain, snow, bolt, shadow, bar;
};
static Theme th;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return tft.color565(r, g, b); }

static void setTheme(bool night) {
  th.night = night;
  th.bg = TFT_BLACK;
  if (!night) {
    th.hdrBg = rgb(20, 45, 100);
    th.fg = TFT_WHITE;
    th.dim = rgb(180, 185, 195);
    th.line = rgb(70, 75, 95);
    th.accent = rgb(90, 205, 255);
    th.sun = rgb(255, 200, 0);
    th.moon = rgb(240, 230, 170);
    th.cloud = rgb(215, 220, 232);
    th.darkCloud = rgb(155, 160, 178);
    th.rain = rgb(70, 155, 255);
    th.snow = TFT_WHITE;
    th.bolt = rgb(255, 225, 0);
    th.shadow = rgb(55, 55, 70);
    th.bar = rgb(25, 70, 150);
    return;
  }
  // Night: one colour on black, so the screen does not light up a dark room.
  uint16_t a, d, l, h, s, b;
  switch (cfg.nightPalette) {
    case NIGHT_RED:
      a = rgb(255, 55, 30); d = rgb(185, 35, 20); l = rgb(75, 12, 5); h = rgb(55, 6, 0); s = rgb(45, 8, 4); b = rgb(80, 14, 6);
      break;
    case NIGHT_GREY:
      a = rgb(205, 205, 205); d = rgb(140, 140, 140); l = rgb(60, 60, 60); h = rgb(38, 38, 42); s = rgb(40, 40, 40); b = rgb(65, 65, 65);
      break;
    default:  // amber
      a = rgb(255, 140, 0); d = rgb(200, 95, 0); l = rgb(85, 35, 0); h = rgb(50, 18, 0); s = rgb(45, 18, 0); b = rgb(80, 32, 0);
  }
  th.hdrBg = h;
  th.fg = th.accent = th.sun = th.moon = th.rain = th.snow = th.bolt = a;
  th.dim = th.cloud = th.darkCloud = d;
  th.line = l;
  th.shadow = s;
  th.bar = b;
}

static bool isNight(const WxData &d) {
  if (cfg.mode == MODE_DAY) return false;
  if (cfg.mode == MODE_NIGHT) return true;
  if (!d.ok || d.nDays == 0) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return !d.now.isDay;
  return now < d.days[0].sunrise || now >= d.days[0].sunset;
}

static void setBacklight(int level) {
  level = constrain(level, 5, 255);
  if (level != lastBright) {
    ledcWrite(BL_CH, level);
    lastBright = level;
  }
}

static void applyRotation() {
  int r = cfg.flip ? 3 : 1;
  if (r != rotation) {
    tft.setRotation(r);
    rotation = r;
  }
}

static bool coloured() { return !th.night && cfg.colours; }

// Value colours, day theme only. Temperature thresholds in Celsius (0, 10, 18, 27, 32 C = 32, 50, 65, 80, 90 F).
static uint16_t tempColor(float c) {
  if (!coloured() || isnan(c)) return th.fg;
  if (c < 0) return rgb(150, 195, 255);
  if (c < 10) return rgb(80, 215, 255);
  if (c < 18.3f) return rgb(120, 235, 140);
  if (c < 26.7f) return rgb(255, 225, 70);
  if (c < 32.2f) return rgb(255, 150, 40);
  return rgb(255, 85, 70);
}
static uint16_t levelColor(int level) {  // 0 good .. 5 very bad
  if (!coloured()) return th.fg;
  static const uint8_t c[6][3] = {{90, 230, 110}, {255, 225, 60}, {255, 150, 40}, {255, 75, 65}, {210, 110, 255}, {200, 60, 110}};
  level = constrain(level, 0, 5);
  return rgb(c[level][0], c[level][1], c[level][2]);
}

// ---------------------------------------------------------------- text helpers

static String num(float v, int decimals = 0) {
  if (isnan(v)) return "--";
  if (decimals == 0) return String((long)lroundf(v));
  return String(v, decimals);
}

static int textW(const String &s, const GFXfont *f) {
  spr.setFreeFont(f);
  return spr.textWidth(s);
}

static int text(const String &s, int x, int y, const GFXfont *f, uint16_t c, uint8_t datum = L_BASELINE) {
  spr.setFreeFont(f);
  spr.setTextColor(c);
  spr.setTextDatum(datum);
  return spr.drawString(s, x, y);
}

static String fit(String s, const GFXfont *f, int maxW) {
  if (textW(s, f) <= maxW) return s;
  while (s.length() > 1 && textW(s + ".", f) > maxW) s.remove(s.length() - 1);
  return s + ".";
}

// Large digit font (font 8 = 72 px, font 6 = 48 px), drawn three times side by side to make the strokes bolder.
static int bigDigits(const String &s, int x, int y, uint16_t c, int font) {
  spr.setTextFont(font);
  spr.setTextColor(c);
  spr.setTextDatum(TL_DATUM);
  for (int dx = 0; dx <= 2; dx++) spr.drawString(s, x + dx, y);
  return spr.textWidth(s) + 2;
}

static void degRing(int x, int y, int r, uint16_t c) {
  spr.fillCircle(x, y, r, c);
  spr.fillCircle(x, y, r / 2, th.bg);
}

static int ringR(const GFXfont *f) { return f == F24 ? 6 : f == F18 ? 4 : 3; }
static int capH(const GFXfont *f) { return f == F24 ? CAP24 : f == F18 ? CAP18 : CAP12; }

// Temperatures: c is Celsius, shown in the configured unit with a degree ring.
static int tempW(float c, const GFXfont *f) { return textW(num(toTemp(c)), f) + 2 * ringR(f) + 4; }

static int tempAt(float c, int x, int y, const GFXfont *f, uint16_t col) {
  int r = ringR(f);
  int w = text(num(toTemp(c)), x, y, f, col);
  degRing(x + w + r + 3, y - capH(f) + r, r, col);
  return w + 2 * r + 4;
}

static void tempCentered(float c, int cx, int y, const GFXfont *f, uint16_t col) {
  tempAt(c, cx - tempW(c, f) / 2, y, f, col);
}

static struct tm localTm(time_t local) {
  struct tm tmv;
  gmtime_r(&local, &tmv);
  return tmv;
}

static String clockStr(time_t local, bool withAmPm = true) {
  struct tm t = localTm(local);
  char b[16];
  if (cfg.clock24) snprintf(b, sizeof(b), "%d:%02d", t.tm_hour, t.tm_min);
  else {
    int h = t.tm_hour % 12;
    if (h == 0) h = 12;
    if (withAmPm) snprintf(b, sizeof(b), "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
    else snprintf(b, sizeof(b), "%d:%02d", h, t.tm_min);
  }
  return b;
}

static String amPm(time_t local) { return localTm(local).tm_hour < 12 ? "AM" : "PM"; }

static String hourLabel(time_t local) {
  struct tm t = localTm(local);
  char b[8];
  if (cfg.clock24) snprintf(b, sizeof(b), "%d:00", t.tm_hour);
  else {
    int h = t.tm_hour % 12;
    snprintf(b, sizeof(b), "%d%s", h == 0 ? 12 : h, t.tm_hour < 12 ? "AM" : "PM");
  }
  return b;
}

static String dayName(time_t local) {
  static const char *n[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return n[localTm(local).tm_wday];
}

static const char *compass(int deg) {
  static const char *n[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  if (deg < 0) return "--";
  return n[((deg * 10 + 112) / 225) % 16];
}

static const char *conditionName(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Icy drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Icy rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Downpours";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder + hail";
  }
  return "";
}

// ---------------------------------------------------------------- icons (drawn, so they scale)

enum IconKind { IK_CLEAR, IK_PARTLY, IK_CLOUD, IK_FOG, IK_DRIZZLE, IK_RAIN, IK_SNOW, IK_THUNDER };

static IconKind iconKind(int code) {
  if (code <= 1) return IK_CLEAR;
  if (code == 2) return IK_PARTLY;
  if (code == 3) return IK_CLOUD;
  if (code == 45 || code == 48) return IK_FOG;
  if (code >= 51 && code <= 57) return IK_DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return IK_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return IK_SNOW;
  if (code >= 95) return IK_THUNDER;
  return IK_CLOUD;
}

static void drawSun(float cx, float cy, float s) {
  float ri = s * 0.36f, ro = s * 0.49f, w = max(2.0f, s / 13.0f);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    spr.drawWideLine(cx + cosf(a) * ri, cy + sinf(a) * ri, cx + cosf(a) * ro, cy + sinf(a) * ro, w, th.sun, th.bg);
  }
  spr.fillCircle(cx, cy, s * 0.27f, th.sun);
}

static void drawMoonIcon(float cx, float cy, float s) {
  float r = s * 0.38f;
  spr.fillCircle(cx, cy, r, th.moon);
  spr.fillCircle(cx + r * 0.55f, cy - r * 0.35f, r * 0.85f, th.bg);
}

static void drawCloud(float cx, float cy, float w, uint16_t c) {
  spr.fillCircle(cx - w * 0.22f, cy + w * 0.02f, w * 0.20f, c);
  spr.fillCircle(cx + w * 0.03f, cy - w * 0.08f, w * 0.27f, c);
  spr.fillCircle(cx + w * 0.27f, cy + w * 0.05f, w * 0.17f, c);
  spr.fillRoundRect(cx - w * 0.42f, cy + w * 0.02f, w * 0.84f, w * 0.20f, w * 0.10f, c);
}

static void drawIcon(int code, bool day, int cx, int cy, int s) {
  IconKind k = iconKind(code);
  switch (k) {
    case IK_CLEAR:
      if (day) drawSun(cx, cy, s); else drawMoonIcon(cx, cy, s);
      break;
    case IK_PARTLY:
      if (day) drawSun(cx + s * 0.18f, cy - s * 0.17f, s * 0.62f);
      else drawMoonIcon(cx + s * 0.18f, cy - s * 0.17f, s * 0.62f);
      drawCloud(cx - s * 0.06f, cy + s * 0.12f, s * 0.84f, th.cloud);
      break;
    case IK_CLOUD:
      drawCloud(cx, cy + s * 0.06f, s, th.cloud);
      break;
    case IK_FOG: {
      drawCloud(cx, cy - s * 0.12f, s * 0.86f, th.darkCloud);
      float w = max(2.0f, s / 13.0f);
      for (int i = 0; i < 3; i++) {
        float y = cy + s * (0.17f + i * 0.13f);
        spr.drawWideLine(cx - s * 0.42f + i * s * 0.06f, y, cx + s * 0.42f - i * s * 0.06f, y, w, th.dim, th.bg);
      }
      break;
    }
    default: {  // rain, drizzle, snow, thunder: dark cloud on top, something falling below
      drawCloud(cx, cy - s * 0.15f, s * 0.92f, th.darkCloud);
      float y0 = cy + s * 0.15f, y1 = cy + s * 0.46f;
      if (k == IK_SNOW) {
        int r = max(2, (int)(s * 0.065f));
        for (int i = 0; i < 3; i++) {
          float x = cx + (i - 1) * s * 0.27f;
          spr.fillCircle(x, y0 + s * 0.06f + (i % 2) * s * 0.15f, r, th.snow);
        }
      } else if (k == IK_THUNDER) {
        spr.fillTriangle(cx + s * 0.06f, y0 - s * 0.04f, cx - s * 0.14f, cy + s * 0.31f, cx + s * 0.02f, cy + s * 0.31f, th.bolt);
        spr.fillTriangle(cx - s * 0.03f, cy + s * 0.27f, cx + s * 0.13f, cy + s * 0.27f, cx - s * 0.07f, cy + s * 0.50f, th.bolt);
      } else {
        float w = max(2.0f, s / (k == IK_DRIZZLE ? 17.0f : 11.0f));
        float len = k == IK_DRIZZLE ? s * 0.15f : (y1 - y0);
        for (int i = 0; i < 3; i++) {
          float x = cx + (i - 1) * s * 0.27f;
          spr.drawWideLine(x + s * 0.06f, y0, x + s * 0.06f - len * 0.35f, y0 + len, w, th.rain, th.bg);
        }
      }
    }
  }
}

// ---------------------------------------------------------------- common parts

static void header(const String &title, const WxData &d) {
  spr.fillRect(0, 0, W, HDR, th.hdrBg);
  time_t ln = localNow(d);
  int clockW = 0;
  if (ln && d.ok) clockW = text(clockStr(ln), W - 6, 23, F12, th.fg, R_BASELINE);
  text(fit(title, F12, W - 20 - clockW), 6, 23, F12, th.fg);
}

static void vline(int x, int y0, int y1) { spr.drawFastVLine(x, y0, y1 - y0, th.line); }
static void hline(int y, int x0, int x1) { spr.drawFastHLine(x0, y, x1 - x0, th.line); }

static String ipText() { return demo ? String("192.168.1.50") : WiFi.localIP().toString(); }

// Shown instead of a page while there is no data yet.
static void noData(const String &title, const WxData &d) {
  header(title, d);
  if (d.locating) {
    text("Finding your location...", W / 2, 95, F18, th.fg, C_BASELINE);
    return;
  }
  if (!cfg.located) {
    text("Set your location at", W / 2, 80, F18, th.fg, C_BASELINE);
    text(ipText(), W / 2, 122, F24, th.accent, C_BASELINE);
    return;
  }
  text("Getting weather...", W / 2, 95, F18, th.fg, C_BASELINE);
  if (d.err[0]) text(fit(d.err, F12, W - 10), W / 2, 135, F12, th.dim, C_BASELINE);
}

static const char *placeName() { return cfg.name.length() ? cfg.name.c_str() : "Weather"; }

// ---------------------------------------------------------------- pages

// Main page: current conditions on the left, the coming hours on the right.
static void pageNow(const WxData &d) {
  if (!d.ok) { noData(placeName(), d); return; }
  header(placeName(), d);

  drawIcon(d.now.code, d.now.isDay, 37, 69, 64);

  String t = num(toTemp(d.now.temp));
  uint16_t tc = tempColor(d.now.temp);
  spr.setTextFont(8);
  int font = spr.textWidth(t) > 106 ? 6 : 8, y = font == 8 ? 34 : 46;
  int w = bigDigits(t, 76, y, tc, font);
  degRing(76 + w + 9, y + 9, 7, tc);

  if (cfg.mainLine2 == LINE2_FEELS) {
    int x = 6 + text("Feels", 6, 137, F18, th.dim) + 10;
    tempAt(d.now.feels, x, 137, F18, tempColor(d.now.feels));
  } else {
    const char *cn = conditionName(d.now.code);
    text(cn, 6, 137, textW(cn, F18) <= 184 ? F18 : F12, th.fg);
  }

  if (d.nDays > 0) {
    int x = 6;
    x += text("H", x, 166, F18, th.dim) + 6;
    x += tempAt(d.days[0].tmax, x, 166, F18, tempColor(d.days[0].tmax)) + 12;
    x += text("L", x, 166, F18, th.dim) + 6;
    tempAt(d.days[0].tmin, x, 166, F18, tempColor(d.days[0].tmin));
  }

  // Coming hours: one row per step, time on the left and temperature on the right.
  vline(194, 36, 166);
  for (int i = 0; i < 3; i++) {
    int idx = d.nowIdx + cfg.mainStep * (i + 1);
    if (idx >= d.nHours) break;
    const HourPt &h = d.hours[idx];
    int y0 = 32 + i * 46;
    if (i) hline(y0, 200, 318);
    text(hourLabel(h.t + d.utcOffset), 200, y0 + 32, F12, th.dim);
    int tw = tempW(h.temp, F18);
    tempAt(h.temp, 320 - tw, y0 + 35, F18, tempColor(h.temp));
  }
}

// Next hours: temperature curve over rain-chance bars.
static void pageHours(const WxData &d) {
  String title = "Next " + String(cfg.hoursSpan) + " hours";
  if (!d.ok) { noData(title, d); return; }
  header(title, d);

  const int x0 = 14, x1 = 306, yTop = 70, yBot = 128, barBase = 142;
  int first = d.nowIdx, last = min(d.nHours - 1, d.nowIdx + (int)cfg.hoursSpan);
  int n = last - first;
  if (n < 2) return;
  float step = (float)(x1 - x0) / n;

  float tmin = 1e9, tmax = -1e9;
  int imin = first, imax = first, maxPop = 0, iPop = first;
  for (int i = first; i <= last; i++) {
    float t = d.hours[i].temp;
    if (t < tmin) { tmin = t; imin = i; }
    if (t > tmax) { tmax = t; imax = i; }
    if (d.hours[i].pop > maxPop) { maxPop = d.hours[i].pop; iPop = i; }
  }
  float span = max(tmax - tmin, 2.0f);
  auto X = [&](int i) { return x0 + (i - first) * step; };
  auto Y = [&](float t) { return yBot - (t - tmin) / span * (yBot - yTop); };

  if (cfg.hoursRain) {
    for (int i = first; i <= last; i++) {
      int p = d.hours[i].pop;
      if (p <= 0) continue;
      int h = p * 60 / 100;
      spr.fillRect(X(i) - step / 2 + 1, barBase - h, max(2.0f, step - 2), h, th.bar);
    }
  }
  hline(barBase, 0, W);

  // time marks: every 3, 6 or 12 hours; midnight shows the day name
  int every = cfg.hoursSpan <= 12 ? 3 : cfg.hoursSpan <= 24 ? 6 : 12;
  for (int i = first; i <= last; i++) {
    struct tm t = localTm(d.hours[i].t + d.utcOffset);
    if (t.tm_hour % every != 0) continue;
    int x = X(i);
    spr.drawFastVLine(x, yTop - 8, barBase - yTop + 8, th.line);
    String lb = t.tm_hour == 0 ? dayName(d.hours[i].t + d.utcOffset) : hourLabel(d.hours[i].t + d.utcOffset);
    int lw = textW(lb, F12);
    int lx = constrain(x - lw / 2, 0, W - lw);
    text(lb, lx, 166, F12, t.tm_hour == 0 ? th.fg : th.dim);
  }

  for (int i = first; i < last; i++) {
    float tm = (d.hours[i].temp + d.hours[i + 1].temp) / 2;
    spr.drawWideLine(X(i), Y(d.hours[i].temp), X(i + 1), Y(d.hours[i + 1].temp), 5, tempColor(tm), th.bg);
  }

  // high and low labels, both above their points
  auto mark = [&](int i) {
    float t = d.hours[i].temp;
    int x = X(i), y = Y(t);
    spr.fillCircle(x, y, 5, tempColor(t));
    int w = tempW(t, F18);
    tempAt(t, constrain(x - w / 2, 0, W - w), y - 9, F18, tempColor(t));
  };
  mark(imax);
  if (imin != imax && fabsf(X(imin) - X(imax)) > 40) mark(imin);

  if (cfg.hoursRain && maxPop >= 20) {
    String p = String(maxPop) + "%";
    int pw = textW(p, F12);
    int px = constrain((int)X(iPop) - pw / 2, 0, W - pw);
    text(p, px, barBase - 4, F12, th.night ? th.fg : th.rain);
  }
}

// Next 3 or 4 days.
static void pageDays(const WxData &d) {
  if (!d.ok) { noData("Next days", d); return; }
  header("Next days", d);
  int start = cfg.daysToday ? 0 : 1;
  int cols = min((int)cfg.daysCount, d.nDays - start);
  if (cols <= 0) return;
  int cw = W / cols;
  int iconS = cols == 3 ? 50 : 44;
  for (int i = 0; i < cols; i++) {
    const DayPt &p = d.days[i + start];
    int cx = i * cw + cw / 2;
    if (i) vline(i * cw, 36, 166);
    text(dayName(p.t + d.utcOffset), cx, 58, F18, th.fg, C_BASELINE);
    drawIcon(p.code, true, cx, 85, iconS);
    const GFXfont *hf = tempW(p.tmax, F24) <= cw - 2 ? F24 : F18;
    tempCentered(p.tmax, cx, 139, hf, tempColor(p.tmax));
    tempCentered(p.tmin, cx, 167, F18, th.night ? th.dim : tempColor(p.tmin));
  }
}

// Wind: compass arrow plus speed, gusts and direction.
static void pageWind(const WxData &d) {
  if (!d.ok) { noData("Wind", d); return; }
  header("Wind", d);

  const int cx = 78, cy = 101, r = 62;
  spr.drawCircle(cx, cy, r, th.line);
  spr.drawCircle(cx, cy, r - 1, th.line);
  text("N", cx, cy - r + 24, F12, th.dim, C_BASELINE);
  text("S", cx, cy + r - 6, F12, th.dim, C_BASELINE);
  text("W", cx - r + 14, cy + 8, F12, th.dim, C_BASELINE);
  text("E", cx + r - 13, cy + 8, F12, th.dim, C_BASELINE);

  float kmh = d.now.wind;
  bool calm = isnan(kmh) || kmh < 2;
  int lvl = kmh < 16 ? 0 : kmh < 32 ? 1 : kmh < 48 ? 2 : 3;
  uint16_t wc = levelColor(lvl);

  if (!calm && d.now.dir >= 0) {
    // Arrow points the way the wind blows (downwind); the dot marks where it comes from.
    float a = d.now.dir * DEG_TO_RAD;
    float ux = sinf(a), uy = -cosf(a);
    float fx = cx + ux * (r - 33), fy = cy + uy * (r - 33);
    float tx = cx - ux * (r - 28), ty = cy - uy * (r - 28);
    float bx = tx + ux * 22, by = ty + uy * 22;
    spr.drawWideLine(fx, fy, bx, by, 8, wc, th.bg);
    spr.fillCircle(fx, fy, 7, wc);
    float px = -uy, py = ux;
    spr.fillTriangle(tx, ty, bx + px * 15, by + py * 15, bx - px * 15, by - py * 15, wc);
  }

  const int xr = 160;
  if (calm) {
    text("Calm", xr + 4, 96, F24, wc);
  } else {
    String s = num(toWind(kmh));
    spr.setTextFont(8);
    int font = spr.textWidth(s) > 106 ? 6 : 8;
    int w = bigDigits(s, xr + 2, font == 8 ? 34 : 50, wc, font);
    text(windUnitName(), xr + 8 + w, 104, F12, th.dim);
  }
  int x = xr + 4;
  x += text("gust", x, 139, F12, th.dim) + 8;
  text(num(toWind(d.now.gust)), x, 139, F18, th.fg);
  x = xr + 4;
  x += text("from", x, 167, F12, th.dim) + 8;
  text(compass(d.now.dir), x, 167, F18, th.accent);
}

// 2 x 2 tiles: feels like, humidity, UV, air quality.
static void tile(int x, int y, const char *label, const String &tag, uint16_t tagColor) {
  text(label, x + 8, y + 24, F12, th.dim);
  if (tag.length()) text(tag, x + 152, y + 24, F12, tagColor, R_BASELINE);
}

static void pageAir(const WxData &d) {
  if (!d.ok) { noData("Comfort & air", d); return; }
  header("Comfort & air", d);
  vline(160, 34, 168);
  hline(100, 4, 316);

  tile(0, 30, "Feels like", "", 0);
  tempAt(d.now.feels, 8, 92, F24, tempColor(d.now.feels));

  tile(160, 30, "Humidity", "", 0);
  text(d.now.hum < 0 ? String("--") : String(d.now.hum) + "%", 168, 92, F24, th.fg);

  static const char *un[] = {"Low", "Medium", "High", "Very high", "Extreme"};
  auto uvLevel = [](float u) { return isnan(u) ? 0 : u < 3 ? 0 : u < 6 ? 1 : u < 8 ? 2 : u < 11 ? 3 : 4; };
  if (!d.now.isDay && d.nDays) {  // at night show today's peak instead
    float m = d.days[0].uvMax;
    tile(0, 100, "UV max", "", 0);
    text(num(m), 8, 162, F24, levelColor(uvLevel(m)));
  } else {
    int ul = uvLevel(d.now.uv);
    tile(0, 100, "UV", un[ul], levelColor(ul));
    text(num(d.now.uv), 8, 162, F24, levelColor(ul));
  }

  if (d.aqOk) {
    int a = d.aqi;
    int al = a <= 50 ? 0 : a <= 100 ? 1 : a <= 150 ? 2 : a <= 200 ? 3 : a <= 300 ? 4 : 5;
    // US AQI categories, shortened to fit: good, moderate, unhealthy for sensitive groups,
    // unhealthy, very unhealthy, hazardous
    static const char *an[] = {"Good", "Fair", "Poor", "Bad", "Very bad", "Hazard"};
    tile(160, 100, "Air", an[al], levelColor(al));
    text(String(a), 168, 162, F24, levelColor(al));
  } else {
    tile(160, 100, "Air", "", 0);
    text("--", 168, 162, F24, th.dim);
  }
}

// Pressure now, its 3 hour trend, and the next 24 hours.
static void pagePressure(const WxData &d) {
  if (!d.ok) { noData("Pressure", d); return; }
  header("Pressure", d);
  int dec = presDecimals();
  int x = 8 + text(num(toPres(d.now.pres), dec), 8, 70, F24, th.fg);
  text(presUnitName(), x + 6, 70, F12, th.dim);

  float tr = d.presTrend;  // hPa per 3 h
  const char *word = "Steady";
  int dir = 0;
  if (!isnan(tr) && fabsf(tr) >= 1.0f) {
    dir = tr > 0 ? 1 : -1;
    word = fabsf(tr) >= 3.5f ? (dir > 0 ? "Rising fast" : "Falling fast") : (dir > 0 ? "Rising" : "Falling");
  }
  uint16_t tc = dir == 0 ? th.fg : (coloured() ? (dir > 0 ? levelColor(0) : levelColor(2)) : th.fg);
  text(word, 40, 104, F18, tc);
  int ax = 19, ay = 92;
  if (dir > 0) spr.fillTriangle(ax, ay - 13, ax - 11, ay + 7, ax + 11, ay + 7, tc);
  else if (dir < 0) spr.fillTriangle(ax, ay + 13, ax - 11, ay - 7, ax + 11, ay - 7, tc);
  else spr.fillRect(ax - 11, ay - 3, 22, 7, tc);

  int first = d.nowIdx, last = min(d.nHours - 1, d.nowIdx + 24);
  if (last - first >= 2) {
    float lo = 1e9, hi = -1e9;
    for (int i = first; i <= last; i++) { lo = min(lo, d.hours[i].pres); hi = max(hi, d.hours[i].pres); }
    float span = max(hi - lo, 4.0f);
    float mid = (hi + lo) / 2;
    const int x0 = 8, x1 = 312, yT = 114, yB = 132;
    float step = (float)(x1 - x0) / (last - first);
    hline(yB + 6, x0, x1);
    for (int i = first; i < last; i++) {
      float ya = (yT + yB) / 2 - (d.hours[i].pres - mid) / span * (yB - yT);
      float yb = (yT + yB) / 2 - (d.hours[i + 1].pres - mid) / span * (yB - yT);
      spr.drawWideLine(x0 + (i - first) * step, ya, x0 + (i + 1 - first) * step, yb, 4, th.accent, th.bg);
    }
    x = 8 + text("In 24 h", 8, 166, F18, th.dim) + 12;
    text(num(toPres(d.hours[last].pres), dec), x, 166, F18, th.fg);
  }
}

// Sunrise / sunset and the moon phase.
static void drawMoonPhase(int cx, int cy, int r, float phase, bool south) {
  spr.fillCircle(cx, cy, r, th.shadow);
  float c = cosf(phase * 2 * PI);
  for (int dy = -r; dy <= r; dy++) {
    float hw = sqrtf((float)(r * r - dy * dy));
    float a, b;
    if (phase < 0.5f) { a = hw * c; b = hw; }    // waxing: lit on the right (northern hemisphere)
    else { a = -hw; b = -hw * c; }               // waning: lit on the left
    if (south) { float t = -b; b = -a; a = t; }  // seen upside down from the southern hemisphere
    if (b > a) spr.drawFastHLine(cx + (int)roundf(a), cy + dy, (int)roundf(b - a) + 1, th.moon);
  }
  spr.drawCircle(cx, cy, r, th.dim);
}

static void pageSun(const WxData &d) {
  if (!d.ok) { noData("Sun & moon", d); return; }
  header("Sun & moon", d);
  time_t now = time(nullptr);
  int di = (d.nDays > 1 && now >= d.days[0].sunset) ? 1 : 0;  // after sunset show tomorrow
  const DayPt &p = d.days[di];

  text(di ? "Sunrise tmrw" : "Sunrise", 8, 54, F12, th.dim);
  int x = 8 + text(clockStr(p.sunrise + d.utcOffset, false), 8, 92, F24, th.sun);
  if (!cfg.clock24) text(amPm(p.sunrise + d.utcOffset), x + 6, 92, F12, th.dim);

  text("Sunset", 8, 124, F12, th.dim);
  x = 8 + text(clockStr(p.sunset + d.utcOffset, false), 8, 162, F24, th.night ? th.fg : rgb(255, 150, 60));
  if (!cfg.clock24) text(amPm(p.sunset + d.utcOffset), x + 6, 162, F12, th.dim);

  // moon: phase from a known new moon (2000-01-06 18:14 UTC), synodic month 29.530589 days
  double days = (now - 947182440.0) / 86400.0;
  float phase = fmod(days / 29.530588853, 1.0);
  if (phase < 0) phase += 1;
  vline(190, 36, 166);
  drawMoonPhase(256, 78, 40, phase, cfg.lat < 0);
  static const char *l1[] = {"New", "Waxing", "First", "Waxing", "Full", "Waning", "Last", "Waning"};
  static const char *l2[] = {"moon", "crescent", "quarter", "gibbous", "moon", "gibbous", "quarter", "crescent"};
  int k = (int)(phase * 8 + 0.5f) % 8;
  text(l1[k], 256, 144, F12, th.fg, C_BASELINE);
  text(l2[k], 256, 166, F12, th.fg, C_BASELINE);
}

// Ocean: water temperature, waves and the next tides.
static void pageOcean(const WxData &d) {
  header("Ocean", d);
  vline(160, 36, 166);
  text("Water", 8, 54, F12, th.dim);
  if (d.marineOk && !isnan(d.sst)) tempAt(d.sst, 8, 92, F24, tempColor(d.sst));
  else text("--", 8, 92, F24, th.dim);
  text("Waves", 8, 124, F12, th.dim);
  if (d.marineOk && !isnan(d.waveH)) {
    int x = 8 + text(num(toLen(d.waveH), 1), 8, 162, F24, th.accent);
    x += text(lenUnitName(), x + 4, 162, F12, th.dim) + 12;
    if (!isnan(d.wavePeriod)) text(num(d.wavePeriod) + "s", x, 162, F12, th.dim);
  } else text("--", 8, 162, F24, th.dim);

  text("Tides", 170, 54, F12, th.dim);
  time_t ln = localNow(d);
  int row = 0;
  for (int i = 0; i < d.nTides && row < 3 && d.tideOk; i++) {
    const TidePt &t = d.tides[i];
    if (t.local < ln) continue;
    int y = 90 + row * 37;
    uint16_t c = t.high ? th.accent : (th.night ? th.dim : th.rain);
    if (t.high) spr.fillTriangle(175, y - 22, 166, y - 3, 184, y - 3, c);
    else spr.fillTriangle(175, y - 3, 166, y - 22, 184, y - 22, c);
    int x = 190 + text(clockStr(t.local, false), 190, y, F18, th.fg);
    if (!cfg.clock24) text(amPm(t.local), x + 3, y, F12, th.dim);
    row++;
  }
  if (row == 0) text("--", 170, 92, F18, th.dim);
}

// Status: where to find the settings page, WiFi, last update.
static void pageInfo(const WxData &d) {
  header("Status", d);
  bool up = WiFi.status() == WL_CONNECTED;
  text("Settings page", 8, 54, F12, th.dim);
  text(up ? ipText() : String("No WiFi"), 8, 88, F18, th.accent);
  long rssi = up ? WiFi.RSSI() : -100;
  const char *q = rssi > -60 ? "strong" : rssi > -72 ? "good" : rssi > -82 ? "weak" : "very weak";
  text("WiFi", 8, 118, F12, th.dim);
  if (up) text(String("signal ") + q, 312, 118, F12, th.dim, R_BASELINE);
  String ssid = demo ? String("MyWiFi") : (up ? WiFi.SSID() : String("-"));
  text(fit(ssid, F18, 300), 8, 148, F18, th.fg);
  String upd = "Not updated yet";
  if (d.ok && d.fetched > 1700000000) {
    long mins = (time(nullptr) - d.fetched) / 60;
    upd = mins < 1 ? "Updated just now" : "Updated " + String(mins) + " min ago";
  }
  text(upd, 8, 168, F12, th.dim);
}

// ---------------------------------------------------------------- public API

void uiBegin() {
  tft.init();
  applyRotation();
  tft.fillScreen(TFT_BLACK);
  ledcSetup(BL_CH, 10000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
  setBacklight(cfg.dayBright);
  spr.setColorDepth(16);
  if (!spr.createSprite(W, H)) Serial.println("[ui] sprite allocation FAILED");
  setTheme(false);
}

// Word-wraps s into at most maxLines lines no wider than maxW.
static int wrap(const String &s, const GFXfont *f, int maxW, String *lines, int maxLines) {
  int n = 0;
  String cur;
  int start = 0;
  while (start <= (int)s.length() && n < maxLines) {
    int sp = s.indexOf(' ', start);
    String word = s.substring(start, sp < 0 ? s.length() : sp);
    String cand = cur.length() ? cur + " " + word : word;
    if (cur.length() && textW(cand, f) > maxW) {
      lines[n++] = cur;
      cur = word;
    } else cur = cand;
    if (sp < 0) break;
    start = sp + 1;
  }
  if (n < maxLines && cur.length()) lines[n++] = fit(cur, f, maxW);
  return n;
}

void uiSplash(const String &title, const String &line1, const String &line2) {
  applyRotation();
  setTheme(cfg.mode == MODE_NIGHT);
  spr.fillSprite(th.bg);
  spr.fillRect(0, 0, W, HDR, th.hdrBg);
  text(title, 6, 23, F12, th.fg);
  // main message: up to two lines in 18 pt; hint below: up to two lines in 12 pt
  String a[2], b[2];
  int na = wrap(line1, F18, W - 16, a, 2);
  int nb = line2.length() ? wrap(line2, F12, W - 16, b, 2) : 0;
  int y = nb ? (na == 1 ? 80 : 66) : (na == 1 ? 108 : 92);
  for (int i = 0; i < na; i++, y += 33) text(a[i], W / 2, y, F18, th.fg, C_BASELINE);
  y += 4 + (nb == 1 ? 6 : 0);
  for (int i = 0; i < nb; i++, y += 24) text(b[i], W / 2, y, F12, th.accent, C_BASELINE);
  spr.pushSprite(0, 0);
  setBacklight(th.night ? cfg.nightBright : cfg.dayBright);
}

void uiPortal(const char *apName, const char *apPass) {
  applyRotation();
  setTheme(cfg.mode == MODE_NIGHT);
  spr.fillSprite(th.bg);
  spr.fillRect(0, 0, W, HDR, th.hdrBg);
  text("WiFi setup", 6, 23, F12, th.fg);
  text("On your phone, join WiFi:", 8, 54, F12, th.dim);
  text(apName, 8, 92, F24, th.accent);
  int x = 8 + text("Password", 8, 126, F12, th.dim) + 10;
  text(apPass, x, 126, F18, th.fg);
  x = 8 + text("then open", 8, 162, F12, th.dim) + 10;
  text("192.168.4.1", x, 162, F18, th.fg);
  spr.pushSprite(0, 0);
  setBacklight(th.night ? cfg.nightBright : cfg.dayBright);
}

void uiToast(const String &msg, uint32_t ms) {
  toastMsg = msg;
  toastUntil = millis() + ms;
}

void uiSetDemo(bool on) { demo = on; }

int uiPages(PageId *out) {
  int n = 0;
  for (int i = 0; i < PG_COUNT; i++)
    if (cfg.shown[cfg.order[i]]) out[n++] = (PageId)cfg.order[i];
  if (n == 0) out[n++] = PG_NOW;
  return n;
}

const uint16_t *uiFrame() { return (const uint16_t *)spr.getPointer(); }

void uiDraw(PageId page, const WxData &d, int night) {
  applyRotation();
  setTheme(night < 0 ? isNight(d) : night == 1);
  spr.fillSprite(th.bg);
  switch (page) {
    case PG_NOW: pageNow(d); break;
    case PG_HOURS: pageHours(d); break;
    case PG_DAYS: pageDays(d); break;
    case PG_WIND: pageWind(d); break;
    case PG_AIR: pageAir(d); break;
    case PG_PRESSURE: pagePressure(d); break;
    case PG_SUN: pageSun(d); break;
    case PG_OCEAN: pageOcean(d); break;
    case PG_INFO: pageInfo(d); break;
    default: break;
  }
  if (toastUntil && millis() < toastUntil) {
    int w = max(180, textW(toastMsg, F24) + 40);
    spr.fillRoundRect((W - w) / 2, 45, w, 80, 14, th.hdrBg);
    spr.drawRoundRect((W - w) / 2, 45, w, 80, 14, th.fg);
    text(toastMsg, W / 2, 100, F24, th.fg, C_BASELINE);
  } else toastUntil = 0;
  spr.pushSprite(0, 0);
  setBacklight(th.night ? cfg.nightBright : cfg.dayBright);
}

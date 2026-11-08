// BigWX - large-print weather display for the LilyGO T-Display S3
//
// Buttons
//   BOOT (GPIO 0):  short = next page          hold 3 s = WiFi setup portal (join another network)
//   KEY  (GPIO 14): short = previous page      hold 1 s = colours: automatic -> day -> night
// Settings page: http://<IP shown on the Status page>/ or http://bigwx.local/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include "settings.h"
#include "weather.h"
#include "ui.h"
#include "webui.h"
#include "version.h"

static const int PIN_POWER = 15;  // keeps the board and display powered on battery
static const int PIN_BOOT = 0;
static const int PIN_KEY = 14;
static const char *AP_NAME = "BigWX-Setup";
static const char *AP_PASS = "weather1";

// ---------------------------------------------------------------- buttons

struct Button {
  uint8_t pin;
  uint32_t longMs;
  bool down = false;
  bool longFired = false;
  uint32_t since = 0;
  Button(uint8_t p, uint32_t l) : pin(p), longMs(l) {}
};

enum Press { NONE, SHORT_PRESS, LONG_PRESS };

static Press poll(Button &b) {
  bool d = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (d && !b.down) { b.down = true; b.since = now; b.longFired = false; return NONE; }
  if (d && !b.longFired && now - b.since >= b.longMs) { b.longFired = true; return LONG_PRESS; }
  if (!d && b.down) {
    b.down = false;
    if (!b.longFired && now - b.since > 40) return SHORT_PRESS;
  }
  return NONE;
}

static Button bootBtn(PIN_BOOT, 3000);
static Button keyBtn(PIN_KEY, 1000);

// ---------------------------------------------------------------- WiFi setup portal

static char latBuf[16], lonBuf[16];
static WiFiManagerParameter pName("name", "Place name (optional, can be set later)", "", 28);
static WiFiManagerParameter pLat("lat", "Latitude (e.g. 51.5074)", "", 15);
static WiFiManagerParameter pLon("lon", "Longitude (e.g. -0.1278)", "", 15);

static void configureManager(WiFiManager &wm) {
  latBuf[0] = lonBuf[0] = 0;
  if (cfg.located) {
    snprintf(latBuf, sizeof(latBuf), "%.4f", cfg.lat);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", cfg.lon);
  }
  pName.setValue(cfg.name.c_str(), 28);
  pLat.setValue(latBuf, 15);
  pLon.setValue(lonBuf, 15);
  wm.addParameter(&pName);
  wm.addParameter(&pLat);
  wm.addParameter(&pLon);
  wm.setTitle("BigWX weather display");
  wm.setSaveParamsCallback([] {
    if (strlen(pName.getValue())) cfg.name = pName.getValue();
    if (strlen(pLat.getValue()) && strlen(pLon.getValue())) {
      cfg.lat = constrain(atof(pLat.getValue()), -90.0, 90.0);
      cfg.lon = constrain(atof(pLon.getValue()), -180.0, 180.0);
      cfg.located = true;
    }
    saveSettings();
  });
  wm.setAPCallback([](WiFiManager *) { uiPortal(AP_NAME, AP_PASS); });
  wm.setConnectTimeout(20);
  wm.setHostname(cfg.hostname.c_str());
}

static void runPortalOnDemand() {
  webStop();
  MDNS.end();
  WiFiManager wm;
  configureManager(wm);
  wm.setConfigPortalTimeout(300);
  uiPortal(AP_NAME, AP_PASS);
  wm.startConfigPortal(AP_NAME, AP_PASS);
  uiSplash("BigWX", "Restarting...");
  delay(1000);
  ESP.restart();
}

// ---------------------------------------------------------------- pages

static PageId pages[PG_COUNT];
static int nPages = 0, pageIdx = 0;
static uint32_t lastInput = 0, lastPageChange = 0;

static void goPage(int delta) {
  nPages = uiPages(pages);
  pageIdx = (pageIdx + delta + nPages) % nPages;
  lastPageChange = millis();
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, HIGH);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_KEY, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.println("BigWX " BIGWX_VERSION);

  loadSettings();
  uiBegin();
  uiSplash("BigWX " BIGWX_VERSION, "Connecting to WiFi...", cfg.name);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFiManager wm;
  configureManager(wm);
  if (!wm.getWiFiIsSaved()) {
    // First start: nothing saved yet, so open the setup portal straight away.
    wm.setConfigPortalTimeout(0);
    wm.autoConnect(AP_NAME, AP_PASS);
  } else {
    // A network is saved: keep trying it (the router may still be booting after a power cut).
    // The setup portal only opens when you hold BOOT for 3 seconds.
    String ssid = wm.getWiFiSSID(true), pass = wm.getWiFiPass(true);
    uiSplash("BigWX " BIGWX_VERSION, "Connecting to " + ssid, "Hold BOOT for WiFi setup");
    Serial.printf("[wifi] connecting to %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t lastTry = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (poll(bootBtn) == LONG_PRESS) runPortalOnDemand();
      if (millis() - lastTry > 12000) {
        Serial.printf("[wifi] still not connected (status %d), retrying\n", WiFi.status());
        WiFi.disconnect();
        delay(200);
        WiFi.begin(ssid.c_str(), pass.c_str());
        lastTry = millis();
      }
      delay(20);
    }
  }
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(cfg.hostname.c_str())) MDNS.addService("http", "tcp", 80);
  webBegin();
  weatherBegin();

  uiSplash("BigWX " BIGWX_VERSION, "Connected", WiFi.localIP().toString());
  delay(2500);
  goPage(0);
  lastInput = millis();
}

void loop() {
  webLoop();
  if (portalRequested) runPortalOnDemand();

  Press b = poll(bootBtn), k = poll(keyBtn);
  if (b != NONE || k != NONE) lastInput = millis();
  if (b == SHORT_PRESS) goPage(+1);
  if (b == LONG_PRESS) runPortalOnDemand();
  if (k == SHORT_PRESS) goPage(-1);
  if (k == LONG_PRESS) {
    cfg.mode = (cfg.mode + 1) % 3;
    saveSettings();
    uiToast(cfg.mode == MODE_AUTO ? "Automatic" : cfg.mode == MODE_DAY ? "Day colours" : "Night colours");
  }

  uint32_t now = millis();
  if (cfg.autoCycle > 0) {
    if (now - lastPageChange >= cfg.autoCycle * 1000UL && now - lastInput >= 5000) goPage(+1);
  } else if (cfg.backToMain > 0 && pageIdx != 0 && now - lastInput >= cfg.backToMain * 1000UL) {
    pageIdx = 0;
  }

  static uint32_t lastDraw = 0;
  static int lastDrawnPage = -1;
  if (now - lastDraw >= 500 || pageIdx != lastDrawnPage) {
    nPages = uiPages(pages);
    if (pageIdx >= nPages) pageIdx = 0;
    WxData d;
    weatherSnapshot(d);
    uiDraw(pages[pageIdx], d);
    lastDraw = now;
    lastDrawnPage = pageIdx;
  }
  delay(10);
}

#pragma once
#include <Arduino.h>
#include "settings.h"
#include "weather.h"

void uiBegin();
void uiSplash(const String &title, const String &line1, const String &line2 = "");
void uiPortal(const char *apName, const char *apPass);
void uiToast(const String &msg, uint32_t ms = 1500);
int  uiPages(PageId *out);              // pages currently shown, in the configured order; returns count
// Draw one frame (page + any toast) and set the backlight. night: -1 = follow settings, 0 = day, 1 = night.
void uiDraw(PageId page, const WxData &d, int night = -1);
const uint16_t *uiFrame();              // last frame, 320 x 170 RGB565 (bytes swapped, as the sprite stores it)
void uiSetDemo(bool on);                // hide the WiFi name and IP address (for screenshots)

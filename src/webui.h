#pragma once
void webBegin();
void webLoop();
void webStop();
extern volatile bool portalRequested;  // set by the settings page: open the WiFi setup portal

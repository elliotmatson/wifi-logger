#pragma once
#include <Arduino.h>

// 0.42" SSD1306 OLED helper. The display is 72x40 visible, addressed as a
// region of a 128x64 SSD1306. We use U8g2's purpose-built ER_72X40 constructor
// which handles the column/page offset for us.

void displayInit();

// Splash shown during boot / before any data is available.
void displaySplash(const String &line1, const String &line2 = "");

// AP / portal screen with SSID and IP for the user to connect to.
void displayPortal(const String &apSsid, const String &apIp);

// Compact runtime telemetry view. ssid/rssi/ip etc. may be empty strings.
struct DisplayStatus
{
    String label; // e.g. "logger-1A2B3C"
    String ssid;  // current AP SSID
    int rssi;     // dBm, 0 if unknown
    String ip;    // local IP as dotted string
    int pingGw;   // ms, -1 if failed
    int pingDns;  // ms, -1 if failed
    bool wifiUp;
    bool influxUp;
    uint16_t bufferedPoints; // points waiting in retry buffer
};

void displayStatus(const DisplayStatus &s);

// Brief banner — shown for ~1.5 s on top of the status screen.
void displayBanner(const String &text);

// Toggle panel normal/inverse mode (for burn-in mitigation scheduling).
void displayToggleInversion();

// Exposed by portal.cpp for QR code
extern String gWifiApPass;

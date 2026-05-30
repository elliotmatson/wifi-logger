#pragma once
#include <Arduino.h>
#include "sampler.h"

// InfluxDB v2 writer using esp_http_client (ESP-IDF built-in).
// Formats points as line-protocol, batches them, and POSTs to /api/v2/write.
// No third-party library required.

void influxBegin();       // construct client + apply WriteOptions
void influxReconfigure(); // call after gConfig changes (e.g. dashboard edit)
bool influxValidate();    // server-side ping; updates influxIsConnected()
bool influxIsConnected();

// Push a connectivity sample. Returns true on immediate success OR successful
// enqueue into the retry buffer. Returns false only if the client is unhealthy.
bool influxLogSample(const Sample &s);

// Push a one-off boot-button event. eventName e.g. "boot_button_short".
bool influxLogEvent(const char *eventName, const String &detail = "");

// Force a flush attempt (e.g. after WiFi reconnect). Returns true if buffer is
// now empty, false if some points remain queued.
bool influxFlush();

// Number of points sitting in the retry buffer right now.
uint16_t influxBufferedPoints();

// Last error string from the client (empty if no error).
String influxLastError();

// Log one entry from a passive WiFi scan (measurement: wifi_scan).
// bssid and ssid are tags; rssi and channel are fields.
bool influxLogScanNetwork(const String &ssid, const String &bssid, int rssi, int channel, bool hidden);

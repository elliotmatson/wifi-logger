#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Result of a single sampling pass. -1 ms means "ping failed".
struct Sample
{
    // WiFi parameters (always filled if connected)
    String ssid;
    String bssid;
    int channel;
    int rssi;
    String ip;
    String gatewayIp;

    // Ping results in milliseconds (-1 on failure)
    int pingGwMs;
    int pingTarget1Ms; // configurable, default 8.8.8.8
    int pingTarget2Ms; // configurable, default 1.1.1.1

    // Misc
    uint32_t uptimeMs;
    uint32_t freeHeap;
    bool wifiConnected;
    time_t timestamp; // unix epoch in seconds (0 if NTP not synced yet)
};

// Run one full sample. Always returns a Sample — fields are zeroed/empty when
// data is unavailable.
Sample samplerRunOnce();

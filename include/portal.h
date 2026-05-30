#pragma once
#include <Arduino.h>
#include "config.h"

// AP-mode captive portal built on tzapu/WiFiManager. Adds custom parameters
// for the device label and InfluxDB connection so the user can fully provision
// a logger on first boot without recompiling.

// Run a blocking AP portal until the user finishes configuration. Returns true
// if WiFi is now connected. On first boot it ALWAYS opens the portal so the
// user can fill in Influx details; on subsequent boots it only opens if WiFi
// credentials are missing.
bool portalRunBlocking();

// Launch the portal on demand (e.g. from a long press or dashboard button)
// without blocking forever — returns after `timeoutSeconds`.
void portalRunOnDemand(uint32_t timeoutSeconds = CFG_PORTAL_ON_DEMAND_TIMEOUT_S);

// Clear only stored WiFi credentials (keeps device config in our NVS namespace).
void portalClearWifiCredentials();

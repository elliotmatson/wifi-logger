#pragma once
#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---------- Defaults and timing tunables ----------
#define CFG_NVS_NAMESPACE "wifilog"
#define CFG_DEFAULT_LABEL_PREFIX "logger-"
#define CFG_DEFAULT_INFLUX_BUCKET "wifi_logs"
#define CFG_DEFAULT_SAMPLE_SECONDS 30
#define CFG_MIN_SAMPLE_SECONDS 5
#define CFG_MAX_SAMPLE_SECONDS 3600
#define CFG_DEFAULT_PING_TARGET_1 "8.8.8.8"
#define CFG_DEFAULT_PING_TARGET_2 "1.1.1.1"
#define CFG_DEFAULT_NTP_SERVER "pool.ntp.org"
#define CFG_NTP_SANITY_EPOCH 1700000000

#define CFG_BOOT_SERIAL_SETTLE_MS 100
#define CFG_WIFI_FAIL_REBOOT_MS 2000
#define CFG_WIFI_OK_SPLASH_MS 500
#define CFG_READY_SPLASH_MS 800
#define CFG_FACTORY_RESET_DELAY_MS 800
#define CFG_CONFIG_RESET_REBOOT_MS 200

#define CFG_NTP_SYNC_RETRIES 20
#define CFG_NTP_SYNC_RETRY_MS 250

#define CFG_DISPLAY_LOCK_TIMEOUT_MS 200
#define CFG_DISPLAY_INVERT_PERIOD_MS 300000

#define CFG_INFLUX_REPROBE_UP_MS 60000
#define CFG_INFLUX_REPROBE_DOWN_MS 15000

#define CFG_PORTAL_CONNECT_TIMEOUT_S 60
#define CFG_PORTAL_ON_DEMAND_TIMEOUT_S 180
#define CFG_PORTAL_AP_IP "192.168.4.1"
#define CFG_PORTAL_AP_SSID_PREFIX "WiFiLogger-"

#define CFG_BUTTON_DEBOUNCE_MS 30
#define CFG_BUTTON_SHORT_MAX_MS 1500
#define CFG_BUTTON_LONG_MIN_MS 3000
#define CFG_BUTTON_FACTORY_MIN_MS 10000
#define CFG_BUTTON_EDGE_QUEUE_LEN 8
#define CFG_BUTTON_EVENT_QUEUE_LEN 4
#define CFG_BUTTON_TASK_STACK 3072
#define CFG_BUTTON_TASK_PRIO 10

#define CFG_PING_COUNT 3
#define CFG_PING_TIMEOUT_MS 1000
#define CFG_PING_INTERVAL_MS 200
#define CFG_PING_DONE_TIMEOUT_MS 4500

#define CFG_TASK_SAMPLE_STACK 6144
#define CFG_TASK_SAMPLE_PRIO 5
#define CFG_TASK_INFLUX_STACK 4096
#define CFG_TASK_INFLUX_PRIO 4
#define CFG_TASK_BUTTON_STACK 4096
#define CFG_TASK_BUTTON_PRIO 6
#define CFG_TASK_DISPLAY_BURNIN_STACK 2048
#define CFG_TASK_DISPLAY_BURNIN_PRIO 3
#define CFG_TASK_LED_STACK 2048
#define CFG_TASK_LED_PRIO 3
#define CFG_TASK_SCAN_STACK 4096
#define CFG_TASK_SCAN_PRIO 3

#define CFG_SCAN_INTERVAL_MS 60000 // 1 minute between scans
#define CFG_SCAN_MAX_NETWORKS 100  // cap points per scan to avoid buffer floods
#define CFG_WIFI_OP_LOCK_TIMEOUT_MS 15000
#define CFG_WIFI_RECONNECT_INTERVAL_MS 5000

#define CFG_LED_TASK_TICK_MS 250
#define CFG_LED_INFLUX_DOWN_PERIOD_TICKS 8
#define CFG_LED_INFLUX_DOWN_ON_TICKS 1

#define CFG_INFLUX_BATCH_SIZE 10
#define CFG_INFLUX_BUFFER_SIZE 720
#define CFG_INFLUX_FLUSH_INTERVAL_S 30
#define CFG_INFLUX_RETRY_INTERVAL_S 5
#define CFG_INFLUX_MAX_RETRY_INTERVAL_S 300
#define CFG_INFLUX_MAX_RETRY_ATTEMPTS 3
#define CFG_INFLUX_HTTP_TIMEOUT_MS 5000

// Runtime config persisted in NVS via Preferences. Set on first boot via
// the WiFiManager captive portal, and editable from the web dashboard.
struct DeviceConfig
{
    // Friendly label shown on the OLED and tagged on every Influx point.
    // e.g. "Sanctuary-NE-Corner" or "Office-103".
    String label;

    // InfluxDB v2 connection
    String influxUrl;    // e.g. https://influx.example.org
    String influxOrg;    // org name
    String influxBucket; // bucket name
    String influxToken;  // API token with write access to the bucket

    // Sampling interval in seconds (default 30)
    uint16_t sampleSeconds;

    // Targets to ping (defaults filled in below)
    String pingTarget1; // typically 8.8.8.8
    String pingTarget2; // typically 1.1.1.1
    // pingTarget0 is always the default gateway (looked up at runtime)

    // Optional: NTP server for accurate timestamps
    String ntpServer; // default "pool.ntp.org"

    bool loaded; // set true once successfully read from NVS
};

extern DeviceConfig gConfig;

// Load config from NVS. Returns false if no config has been saved yet.
bool configLoad();

// Save current gConfig to NVS.
void configSave();

// Reset config to factory defaults (does NOT touch WiFi credentials —
// WiFiManager owns those in its own NVS namespace).
void configResetDefaults();

// Wipe everything including WiFi credentials and reboot.
void configFactoryReset();

// Helper: 6-char hex suffix of the chip MAC, useful for default labels & AP SSID.
String chipIdSuffix();

#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifdef LOCAL_DEFAULT_LABEL
static const char *kLocalDefaultLabel = LOCAL_DEFAULT_LABEL;
#else
static const char *kLocalDefaultLabel = "";
#endif

#ifdef LOCAL_DEFAULT_INFLUX_URL
static const char *kLocalDefaultInfluxUrl = LOCAL_DEFAULT_INFLUX_URL;
#else
static const char *kLocalDefaultInfluxUrl = "";
#endif

#ifdef LOCAL_DEFAULT_INFLUX_ORG
static const char *kLocalDefaultInfluxOrg = LOCAL_DEFAULT_INFLUX_ORG;
#else
static const char *kLocalDefaultInfluxOrg = "";
#endif

#ifdef LOCAL_DEFAULT_INFLUX_BUCKET
static const char *kLocalDefaultInfluxBucket = LOCAL_DEFAULT_INFLUX_BUCKET;
#else
static const char *kLocalDefaultInfluxBucket = "";
#endif

#ifdef LOCAL_DEFAULT_INFLUX_TOKEN
static const char *kLocalDefaultInfluxToken = LOCAL_DEFAULT_INFLUX_TOKEN;
#else
static const char *kLocalDefaultInfluxToken = "";
#endif

DeviceConfig gConfig;

static const char *TAG = "config";
static const char *NVS_NS = CFG_NVS_NAMESPACE;

String chipIdSuffix()
{
    uint64_t mac = ESP.getEfuseMac();
    // Last 24 bits of the MAC, hex, uppercase, zero-padded to 6 chars.
    char buf[7];
    snprintf(buf, sizeof(buf), "%06X", (uint32_t)(mac & 0xFFFFFF));
    return String(buf);
}

void configResetDefaults()
{
    if (kLocalDefaultLabel[0] != '\0')
        gConfig.label = kLocalDefaultLabel;
    else
        gConfig.label = String(CFG_DEFAULT_LABEL_PREFIX) + chipIdSuffix();

    gConfig.influxUrl = kLocalDefaultInfluxUrl;
    gConfig.influxOrg = kLocalDefaultInfluxOrg;

    if (kLocalDefaultInfluxBucket[0] != '\0')
        gConfig.influxBucket = kLocalDefaultInfluxBucket;
    else
        gConfig.influxBucket = CFG_DEFAULT_INFLUX_BUCKET;

    gConfig.influxToken = kLocalDefaultInfluxToken;
    gConfig.sampleSeconds = CFG_DEFAULT_SAMPLE_SECONDS;
    gConfig.pingTarget1 = CFG_DEFAULT_PING_TARGET_1;
    gConfig.pingTarget2 = CFG_DEFAULT_PING_TARGET_2;
    gConfig.ntpServer = CFG_DEFAULT_NTP_SERVER;
    gConfig.loaded = false;
}

bool configLoad()
{
    configResetDefaults();
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/true))
    {
        return false;
    }
    bool has = prefs.isKey("label");
    if (has)
    {
        gConfig.label = prefs.getString("label", gConfig.label);
        gConfig.influxUrl = prefs.getString("influxUrl", gConfig.influxUrl);
        gConfig.influxOrg = prefs.getString("influxOrg", gConfig.influxOrg);
        gConfig.influxBucket = prefs.getString("influxBucket", gConfig.influxBucket);
        gConfig.influxToken = prefs.getString("influxToken", gConfig.influxToken);
        gConfig.sampleSeconds = prefs.getUShort("sampleSec", gConfig.sampleSeconds);
        gConfig.pingTarget1 = prefs.getString("ping1", gConfig.pingTarget1);
        gConfig.pingTarget2 = prefs.getString("ping2", gConfig.pingTarget2);
        gConfig.ntpServer = prefs.getString("ntpServer", gConfig.ntpServer);
        gConfig.loaded = true;
    }
    prefs.end();
    return gConfig.loaded;
}

void configSave()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/false))
    {
        ESP_LOGE(TAG, "configSave: failed to open NVS");
        return;
    }
    prefs.putString("label", gConfig.label);
    prefs.putString("influxUrl", gConfig.influxUrl);
    prefs.putString("influxOrg", gConfig.influxOrg);
    prefs.putString("influxBucket", gConfig.influxBucket);
    prefs.putString("influxToken", gConfig.influxToken);
    prefs.putUShort("sampleSec", gConfig.sampleSeconds);
    prefs.putString("ping1", gConfig.pingTarget1);
    prefs.putString("ping2", gConfig.pingTarget2);
    prefs.putString("ntpServer", gConfig.ntpServer);
    prefs.end();
    gConfig.loaded = true;
}

void configFactoryReset()
{
    Preferences prefs;
    if (prefs.begin(NVS_NS, false))
    {
        prefs.clear();
        prefs.end();
    }
    // Erase WiFiManager's stored creds too:
    WiFi.disconnect(/*wifioff=*/true, /*eraseAP=*/true);
    vTaskDelay(pdMS_TO_TICKS(CFG_CONFIG_RESET_REBOOT_MS));
    ESP.restart();
}

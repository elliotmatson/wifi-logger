#include "portal.h"
#include "config.h"
#include "display.h"
#include <esp_log.h>
#include <WiFi.h>
#include <WiFiManager.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef LOCAL_DEFAULT_WIFI_SSID
#define LOCAL_DEFAULT_WIFI_SSID ""
#endif
#ifndef LOCAL_DEFAULT_WIFI_PASS
#define LOCAL_DEFAULT_WIFI_PASS ""
#endif

static const char *TAG = "portal";

static WiFiManager wm;

// Exposed for display QR code
String gWifiApPass;

// String-buffer params — WiFiManager stores pointers to char arrays, so we keep
// these alive for the lifetime of the program. Sized generously.
static char buf_label[40];
static char buf_url[128];
static char buf_org[40];
static char buf_bucket[40];
static char buf_token[200];
static char buf_sample[8];
static char buf_ping1[40];
static char buf_ping2[40];

static WiFiManagerParameter *p_label;
static WiFiManagerParameter *p_url;
static WiFiManagerParameter *p_org;
static WiFiManagerParameter *p_bucket;
static WiFiManagerParameter *p_token;
static WiFiManagerParameter *p_sample;
static WiFiManagerParameter *p_ping1;
static WiFiManagerParameter *p_ping2;

static void buildParams()
{
    strncpy(buf_label, gConfig.label.c_str(), sizeof(buf_label));
    strncpy(buf_url, gConfig.influxUrl.c_str(), sizeof(buf_url));
    strncpy(buf_org, gConfig.influxOrg.c_str(), sizeof(buf_org));
    strncpy(buf_bucket, gConfig.influxBucket.c_str(), sizeof(buf_bucket));
    strncpy(buf_token, gConfig.influxToken.c_str(), sizeof(buf_token));
    snprintf(buf_sample, sizeof(buf_sample), "%u", gConfig.sampleSeconds);
    strncpy(buf_ping1, gConfig.pingTarget1.c_str(), sizeof(buf_ping1));
    strncpy(buf_ping2, gConfig.pingTarget2.c_str(), sizeof(buf_ping2));

    // (Re)create parameters. WiFiManager owns the pointers once added so we
    // only build these once per boot.
    static bool built = false;
    if (built)
        return;
    p_label = new WiFiManagerParameter("label", "Device label (e.g. Lobby-NE)", buf_label, sizeof(buf_label));
    p_url = new WiFiManagerParameter("influx_url", "InfluxDB URL (https://...)", buf_url, sizeof(buf_url));
    p_org = new WiFiManagerParameter("influx_org", "InfluxDB org", buf_org, sizeof(buf_org));
    p_bucket = new WiFiManagerParameter("influx_bucket", "InfluxDB bucket", buf_bucket, sizeof(buf_bucket));
    p_token = new WiFiManagerParameter("influx_token", "InfluxDB API token", buf_token, sizeof(buf_token));
    p_sample = new WiFiManagerParameter("sample_sec", "Sample interval (s)", buf_sample, sizeof(buf_sample));
    p_ping1 = new WiFiManagerParameter("ping1", "Ping target 1 (default 8.8.8.8)", buf_ping1, sizeof(buf_ping1));
    p_ping2 = new WiFiManagerParameter("ping2", "Ping target 2 (default 1.1.1.1)", buf_ping2, sizeof(buf_ping2));

    wm.addParameter(p_label);
    wm.addParameter(p_url);
    wm.addParameter(p_org);
    wm.addParameter(p_bucket);
    wm.addParameter(p_token);
    wm.addParameter(p_sample);
    wm.addParameter(p_ping1);
    wm.addParameter(p_ping2);
    built = true;
}

static void saveParamsCallback()
{
    gConfig.label = p_label->getValue();
    gConfig.influxUrl = p_url->getValue();
    gConfig.influxOrg = p_org->getValue();
    gConfig.influxBucket = p_bucket->getValue();
    gConfig.influxToken = p_token->getValue();
    gConfig.sampleSeconds = constrain(atoi(p_sample->getValue()), CFG_MIN_SAMPLE_SECONDS, CFG_MAX_SAMPLE_SECONDS);
    gConfig.pingTarget1 = p_ping1->getValue();
    gConfig.pingTarget2 = p_ping2->getValue();
    configSave();
    ESP_LOGI(TAG, "portal: config saved (label=%s, url=%s, bucket=%s, sample=%us)",
             gConfig.label.c_str(), gConfig.influxUrl.c_str(),
             gConfig.influxBucket.c_str(), gConfig.sampleSeconds);
}

static void apModeCallback(WiFiManager *w)
{
    String ssid = String(CFG_PORTAL_AP_SSID_PREFIX) + chipIdSuffix();
    displayPortal(ssid, CFG_PORTAL_AP_IP);
}

bool portalRunBlocking()
{
    // Set AP password to blank (open network)
    gWifiApPass = "";
    buildParams();
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setAPCallback(apModeCallback);
    wm.setConfigPortalBlocking(true);
    wm.setConnectTimeout(CFG_PORTAL_CONNECT_TIMEOUT_S);
    wm.setConfigPortalTimeout(0); // 0 = no timeout in blocking mode
    wm.setTitle("WiFi Logger setup");
    wm.setDarkMode(true);

    if (strlen(LOCAL_DEFAULT_WIFI_SSID) > 0)
    {
        wm.preloadWiFi(LOCAL_DEFAULT_WIFI_SSID, LOCAL_DEFAULT_WIFI_PASS);
        ESP_LOGI(TAG, "portal: preloaded default WiFi SSID '%s'", LOCAL_DEFAULT_WIFI_SSID);
    }

    // If no Influx URL is configured yet, force portal regardless of WiFi creds:
    bool needPortal = (gConfig.influxUrl.length() == 0);
    bool ok;
    String ssid = String(CFG_PORTAL_AP_SSID_PREFIX) + chipIdSuffix();
    if (needPortal)
    {
        displayPortal(ssid, CFG_PORTAL_AP_IP);
        ok = wm.startConfigPortal(ssid.c_str());
    }
    else
    {
        ok = wm.autoConnect(ssid.c_str());
    }
    return ok && WiFi.status() == WL_CONNECTED;
}

void portalRunOnDemand(uint32_t timeoutSeconds)
{
    gWifiApPass = "";
    buildParams();
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setAPCallback(apModeCallback);
    wm.setConfigPortalBlocking(true);
    wm.setConfigPortalTimeout(timeoutSeconds);
    wm.setDarkMode(true);
    String ssid = String(CFG_PORTAL_AP_SSID_PREFIX) + chipIdSuffix();
    displayPortal(ssid, CFG_PORTAL_AP_IP);
    wm.startConfigPortal(ssid.c_str());
}

void portalClearWifiCredentials()
{
    // Only clear WiFiManager/SDK STA credentials; keep app config untouched.
    wm.resetSettings();
    WiFi.disconnect(true, true);
    ESP_LOGW(TAG, "portal: WiFi credentials cleared");
}

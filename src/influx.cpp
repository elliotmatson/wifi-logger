#include "influx.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <deque>

static const char *TAG = "influx";

static bool gConnectedFlag = false;
static String gLastError;
// Each entry is one fully-formatted InfluxDB line-protocol line.
static std::deque<String> gBuffer;

// ---------------------------------------------------------------------------
// Line-protocol helpers
// ---------------------------------------------------------------------------

// Escape measurement name, tag key, or tag value (escape , = and space).
static String lpEscapeTag(const String &s)
{
    String out;
    out.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c == ',' || c == ' ' || c == '=')
            out += '\\';
        out += c;
    }
    return out;
}

// Append a tag to a line (skips empty values).
static void lpAddTag(String &line, const char *key, const String &val)
{
    if (val.length() == 0)
        return;
    line += ',';
    line += lpEscapeTag(String(key));
    line += '=';
    line += lpEscapeTag(val);
}

// Append a quoted string field value.
static void lpAddFieldStr(String &line, const char *key, const String &val, bool &first)
{
    if (!first)
        line += ',';
    first = false;
    line += key;
    line += "=\"";
    for (size_t i = 0; i < val.length(); i++)
    {
        char c = val[i];
        if (c == '"' || c == '\\')
            line += '\\';
        line += c;
    }
    line += '"';
}

// Append an integer field value (InfluxDB requires the 'i' suffix).
static void lpAddFieldInt(String &line, const char *key, long val, bool &first)
{
    if (!first)
        line += ',';
    first = false;
    line += key;
    line += '=';
    line += String(val);
    line += 'i';
}

// Append a boolean field value.
static void lpAddFieldBool(String &line, const char *key, bool val, bool &first)
{
    if (!first)
        line += ',';
    first = false;
    line += key;
    line += '=';
    line += (val ? "true" : "false");
}

// Append a unix-second timestamp.
static void lpAddTimestamp(String &line, time_t ts)
{
    line += ' ';
    line += String((long)ts);
}

// ---------------------------------------------------------------------------
// HTTP POST to /api/v2/write
// ---------------------------------------------------------------------------

static bool doPost(const String &body)
{
    if (gConfig.influxUrl.length() == 0 || gConfig.influxToken.length() == 0 ||
        gConfig.influxOrg.length() == 0 || gConfig.influxBucket.length() == 0)
    {
        gLastError = "missing config";
        return false;
    }

    String url = gConfig.influxUrl;
    if (url.endsWith("/"))
        url.remove(url.length() - 1);
    url += "/api/v2/write?precision=s&org=";
    url += gConfig.influxOrg;
    url += "&bucket=";
    url += gConfig.influxBucket;

    String authHeader = "Token ";
    authHeader += gConfig.influxToken;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = CFG_INFLUX_HTTP_TIMEOUT_MS;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        gLastError = "esp_http_client_init failed";
        return false;
    }

    esp_http_client_set_header(client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");

    esp_err_t err = esp_http_client_open(client, (int)body.length());
    if (err != ESP_OK)
    {
        gLastError = String("open: ") + esp_err_to_name(err);
        esp_http_client_cleanup(client);
        return false;
    }

    int written = esp_http_client_write(client, body.c_str(), (int)body.length());
    if (written < 0)
    {
        gLastError = "write failed";
        esp_http_client_cleanup(client);
        return false;
    }

    int contentLen = esp_http_client_fetch_headers(client);
    if (contentLen < 0)
    {
        gLastError = "fetch_headers failed";
        esp_http_client_cleanup(client);
        return false;
    }

    int code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (code >= 200 && code < 300)
    {
        gLastError = "";
        return true;
    }
    gLastError = String("HTTP ") + String(code);
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void influxBegin() { /* nothing to construct */ }

void influxReconfigure()
{
    gConnectedFlag = false;
    gLastError = "";
    gBuffer.clear();
}

bool influxValidate()
{
    if (gConfig.influxUrl.length() == 0)
        return false;

    String url = gConfig.influxUrl;
    if (url.endsWith("/"))
        url.remove(url.length() - 1);
    url += "/health";

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = CFG_INFLUX_HTTP_TIMEOUT_MS;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        gConnectedFlag = false;
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    gConnectedFlag = (err == ESP_OK && code >= 200 && code < 300);
    if (!gConnectedFlag)
        ESP_LOGW(TAG, "influx: health check failed (err=%s, code=%d)", esp_err_to_name(err), code);
    return gConnectedFlag;
}

bool influxIsConnected() { return gConnectedFlag; }

static bool enqueueAndMaybeFlush(const String &line)
{
    if ((int)gBuffer.size() >= CFG_INFLUX_BUFFER_SIZE)
    {
        gBuffer.pop_front(); // drop oldest to make room
        ESP_LOGW(TAG, "influx: buffer full, dropping oldest point");
    }
    gBuffer.push_back(line);
    if ((int)gBuffer.size() >= CFG_INFLUX_BATCH_SIZE)
        return influxFlush();
    return true;
}

bool influxLogSample(const Sample &s)
{
    String line = "wifi_connectivity";
    lpAddTag(line, "device", gConfig.label);
    lpAddTag(line, "chip_id", chipIdSuffix());
    // SSID is a tag for easy dashboard grouping; BSSID stays a field.
    if (s.ssid.length())
        lpAddTag(line, "ssid", s.ssid);

    line += ' ';
    bool first = true;
    lpAddFieldInt(line, "rssi", s.rssi, first);
    lpAddFieldInt(line, "channel", s.channel, first);
    lpAddFieldStr(line, "bssid", s.bssid, first);
    lpAddFieldStr(line, "ip", s.ip, first);
    lpAddFieldStr(line, "gateway", s.gatewayIp, first);
    lpAddFieldInt(line, "ping_gateway_ms", s.pingGwMs, first);
    lpAddFieldInt(line, "ping_google_ms", s.pingTarget1Ms, first);
    lpAddFieldInt(line, "ping_cloudflare_ms", s.pingTarget2Ms, first);
    lpAddFieldBool(line, "ping_gw_ok", s.pingGwMs >= 0, first);
    lpAddFieldBool(line, "ping_google_ok", s.pingTarget1Ms >= 0, first);
    lpAddFieldBool(line, "ping_cloudflare_ok", s.pingTarget2Ms >= 0, first);
    lpAddFieldInt(line, "uptime_s", (long)(s.uptimeMs / 1000UL), first);
    lpAddFieldInt(line, "free_heap", (long)s.freeHeap, first);

    if (s.timestamp > 0)
        lpAddTimestamp(line, s.timestamp);

    return enqueueAndMaybeFlush(line);
}

bool influxLogEvent(const char *eventName, const String &detail)
{
    String line = "wifi_event";
    lpAddTag(line, "device", gConfig.label);
    lpAddTag(line, "chip_id", chipIdSuffix());
    lpAddTag(line, "event", String(eventName));

    line += ' ';
    bool first = true;
    if (detail.length())
        lpAddFieldStr(line, "detail", detail, first);
    lpAddFieldInt(line, "rssi", (long)WiFi.RSSI(), first);
    lpAddFieldStr(line, "ssid", WiFi.SSID(), first);

    time_t now = time(nullptr);
    if (now > CFG_NTP_SANITY_EPOCH)
        lpAddTimestamp(line, now);

    return enqueueAndMaybeFlush(line);
}

bool influxLogScanNetwork(const String &ssid, const String &bssid, int rssi, int channel, bool hidden)
{
    String line = "wifi_scan";
    lpAddTag(line, "device", gConfig.label);
    lpAddTag(line, "chip_id", chipIdSuffix());
    lpAddTag(line, "ssid", ssid);
    lpAddTag(line, "bssid", bssid);

    line += ' ';
    bool first = true;
    lpAddFieldInt(line, "rssi", rssi, first);
    lpAddFieldInt(line, "channel", channel, first);
    lpAddFieldBool(line, "hidden", hidden, first);

    time_t now = time(nullptr);
    if (now > CFG_NTP_SANITY_EPOCH)
        lpAddTimestamp(line, now);

    return enqueueAndMaybeFlush(line);
}

bool influxFlush()
{
    if (gBuffer.empty())
        return true;

    // Join all buffered lines into one request body.
    String body;
    body.reserve(gBuffer.size() * 80);
    for (const auto &line : gBuffer)
    {
        body += line;
        body += '\n';
    }

    bool ok = doPost(body);
    if (ok)
    {
        gBuffer.clear();
        gConnectedFlag = true;
    }
    else
    {
        gConnectedFlag = false;
        ESP_LOGW(TAG, "influx: flush failed: %s", gLastError.c_str());
    }
    return ok;
}

uint16_t influxBufferedPoints()
{
    return (uint16_t)gBuffer.size();
}

String influxLastError()
{
    return gLastError;
}

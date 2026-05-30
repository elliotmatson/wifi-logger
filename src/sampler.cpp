#include "sampler.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ping/ping_sock.h>
#include <lwip/ip_addr.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

struct PingContext
{
    SemaphoreHandle_t done;
    uint32_t replies;
    uint32_t sumMs;
};

static void onPingSuccess(esp_ping_handle_t hdl, void *args)
{
    auto *ctx = static_cast<PingContext *>(args);
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    ctx->replies++;
    ctx->sumMs += elapsed;
}

static void onPingEnd(esp_ping_handle_t hdl, void *args)
{
    auto *ctx = static_cast<PingContext *>(args);
    xSemaphoreGive(ctx->done);
}

// Run 3 echo requests with a 1 s timeout and return average RTT in ms.
// Returns -1 if all requests failed or the session setup failed.
static int pingMs(const IPAddress &addr)
{
    if (addr == IPAddress(0, 0, 0, 0))
        return -1;

    ip_addr_t targetAddr;
    if (!ipaddr_aton(addr.toString().c_str(), &targetAddr))
        return -1;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = targetAddr;
    cfg.count = CFG_PING_COUNT;
    cfg.timeout_ms = CFG_PING_TIMEOUT_MS;
    cfg.interval_ms = CFG_PING_INTERVAL_MS;

    PingContext ctx;
    ctx.done = xSemaphoreCreateBinary();
    ctx.replies = 0;
    ctx.sumMs = 0;
    if (!ctx.done)
        return -1;

    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = &ctx;
    cbs.on_ping_success = onPingSuccess;
    cbs.on_ping_timeout = nullptr;
    cbs.on_ping_end = onPingEnd;

    esp_ping_handle_t session = nullptr;
    if (esp_ping_new_session(&cfg, &cbs, &session) != ESP_OK)
    {
        vSemaphoreDelete(ctx.done);
        return -1;
    }

    esp_ping_start(session);
    xSemaphoreTake(ctx.done, pdMS_TO_TICKS(CFG_PING_DONE_TIMEOUT_MS));
    esp_ping_stop(session);
    esp_ping_delete_session(session);
    vSemaphoreDelete(ctx.done);

    if (ctx.replies == 0)
        return -1;
    return (int)((ctx.sumMs + (ctx.replies / 2)) / ctx.replies);
}

static int pingMs(const String &host)
{
    if (!host.length())
        return -1;
    IPAddress ip;
    if (ip.fromString(host))
        return pingMs(ip);
    // Hostname — resolve via DNS first
    IPAddress resolved;
    if (!WiFi.hostByName(host.c_str(), resolved))
        return -1;
    return pingMs(resolved);
}

static String bssidString()
{
    uint8_t *b = WiFi.BSSID();
    if (!b)
        return "";
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    return String(buf);
}

Sample samplerRunOnce()
{
    Sample s = {};
    s.uptimeMs = millis();
    s.freeHeap = ESP.getFreeHeap();
    s.wifiConnected = (WiFi.status() == WL_CONNECTED);
    time_t now = time(nullptr);
    if (now > CFG_NTP_SANITY_EPOCH)
        s.timestamp = now; // sane only if NTP synced

    if (!s.wifiConnected)
    {
        s.pingGwMs = s.pingTarget1Ms = s.pingTarget2Ms = -1;
        return s;
    }

    s.ssid = WiFi.SSID();
    s.bssid = bssidString();
    s.channel = WiFi.channel();
    s.rssi = WiFi.RSSI();
    s.ip = WiFi.localIP().toString();
    s.gatewayIp = WiFi.gatewayIP().toString();

    // Pings — gateway first (cheap/local), then external targets
    s.pingGwMs = pingMs(WiFi.gatewayIP());
    s.pingTarget1Ms = pingMs(gConfig.pingTarget1);
    s.pingTarget2Ms = pingMs(gConfig.pingTarget2);

    return s;
}

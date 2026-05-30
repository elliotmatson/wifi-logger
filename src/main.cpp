// ESP32-C3 WiFi connectivity logger (RTOS task model)
// ------------------------------------------------------------------
// Boot flow:
//   1. Display splash
//   2. Load NVS config + WiFiManager creds
//   3. If first boot or no Influx URL → AP portal (blocking)
//   4. Connect WiFi → NTP sync → Influx client
//   5. Start dedicated FreeRTOS tasks for sampling, Influx probing, and
//      interrupt-driven button events.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "display.h"
#include "portal.h"
#include "sampler.h"
#include "influx.h"
#include "button.h"
#include "pins.h"

static const char *TAG = "main";

static Sample gLastSample = {};
static SemaphoreHandle_t gDisplayMutex = nullptr;
static SemaphoreHandle_t gWifiOpMutex = nullptr;

// ─── NTP ──────────────────────────────────────────────────────────────────────
static void ntpBegin()
{
    configTime(0, 0, gConfig.ntpServer.c_str(), "time.google.com", "time.cloudflare.com");
}

static bool ntpSynced()
{
    return time(nullptr) > CFG_NTP_SANITY_EPOCH;
}

#if PIN_USER_LED >= 0
static void setUserLed(bool on)
{
#if USER_LED_ACTIVE_LOW
    digitalWrite(PIN_USER_LED, on ? LOW : HIGH);
#else
    digitalWrite(PIN_USER_LED, on ? HIGH : LOW);
#endif
}

static void ledTask(void *)
{
    uint32_t tickPhase = 0;
    while (true)
    {
        bool wifiUp = (WiFi.status() == WL_CONNECTED);
        bool influxUp = influxIsConnected();

        if (!wifiUp)
        {
            // Fast blink while disconnected from WiFi.
            setUserLed((tickPhase & 0x1U) == 0);
        }
        else if (!influxUp)
        {
            // Short pulse while WiFi is up but backend is down.
            uint32_t p = (tickPhase % CFG_LED_INFLUX_DOWN_PERIOD_TICKS);
            setUserLed(p < CFG_LED_INFLUX_DOWN_ON_TICKS);
        }
        else
        {
            // Solid ON when everything is healthy.
            setUserLed(true);
        }

        tickPhase++;
        vTaskDelay(pdMS_TO_TICKS(CFG_LED_TASK_TICK_MS));
    }
}
#endif

static void sampleTask(void *)
{
    bool wifiUpPrev = (WiFi.status() == WL_CONNECTED);
    String prevBssid = wifiUpPrev ? WiFi.BSSIDstr() : "";

    while (true)
    {
        bool sampled = false;
        if (xSemaphoreTake(gWifiOpMutex, pdMS_TO_TICKS(CFG_WIFI_OP_LOCK_TIMEOUT_MS)) == pdTRUE)
        {
            bool wifiUp = (WiFi.status() == WL_CONNECTED);
            if (wifiUp != wifiUpPrev)
            {
                wifiUpPrev = wifiUp;
                if (wifiUp)
                {
                    String bssid = WiFi.BSSIDstr();
                    String detail = "ssid=" + WiFi.SSID() + " bssid=" + bssid + " ip=" + WiFi.localIP().toString();
                    influxLogEvent("wifi_connected", detail);
                    ESP_LOGI(TAG, "WiFi connected: %s", detail.c_str());
                    prevBssid = bssid;
                    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
                    {
                        displayBanner("WiFi back");
                        xSemaphoreGive(gDisplayMutex);
                    }
                    influxFlush();
                }
                else
                {
                    influxLogEvent("wifi_disconnected", "ssid=" + WiFi.SSID());
                    ESP_LOGW(TAG, "WiFi disconnected");
                    prevBssid = "";
                    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
                    {
                        displayBanner("WiFi LOST");
                        xSemaphoreGive(gDisplayMutex);
                    }
                }
            }
            else if (wifiUp)
            {
                // Detect roaming: same SSID, different BSSID.
                String bssid = WiFi.BSSIDstr();
                if (prevBssid.length() > 0 && bssid.length() > 0 && bssid != prevBssid)
                {
                    String detail = "ssid=" + WiFi.SSID() + " from=" + prevBssid + " to=" + bssid;
                    influxLogEvent("wifi_roam", detail);
                    ESP_LOGI(TAG, "WiFi roam: %s", detail.c_str());
                    prevBssid = bssid;
                }
            }

            gLastSample = samplerRunOnce();
            sampled = true;
            xSemaphoreGive(gWifiOpMutex);
        }
        else
        {
            ESP_LOGW(TAG, "sample: WiFi op busy, skipping this sample");
        }

        if (sampled)
        {
            influxLogSample(gLastSample);
            if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
            {
                DisplayStatus ds;
                ds.label = gConfig.label;
                ds.ssid = gLastSample.ssid;
                ds.rssi = gLastSample.rssi;
                ds.ip = gLastSample.ip;
                ds.pingGw = gLastSample.pingGwMs;
                ds.pingDns = gLastSample.pingTarget1Ms;
                ds.wifiUp = WiFi.status() == WL_CONNECTED;
                ds.influxUp = influxIsConnected();
                ds.bufferedPoints = influxBufferedPoints();
                displayStatus(ds);
                xSemaphoreGive(gDisplayMutex);
            }
        }

        uint32_t sampleMs = (uint32_t)gConfig.sampleSeconds * 1000UL;
        vTaskDelay(pdMS_TO_TICKS(sampleMs));
    }
}

static void influxProbeTask(void *)
{
    while (true)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            bool ok = influxValidate();
            if (ok)
                influxFlush();
        }
        uint32_t reprobeMs = influxIsConnected() ? CFG_INFLUX_REPROBE_UP_MS : CFG_INFLUX_REPROBE_DOWN_MS;
        vTaskDelay(pdMS_TO_TICKS(reprobeMs));
    }
}
static void buttonTask(void *)
{
    while (true)
    {
        ButtonEvent be = ButtonEvent::None;
        if (!buttonGetEvent(&be, portMAX_DELAY))
        {
            continue;
        }

        switch (be)
        {
        case ButtonEvent::Short:
            if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
            {
                displayBanner("button");
                xSemaphoreGive(gDisplayMutex);
            }
            influxLogEvent("boot_button_short");
            ESP_LOGI(TAG, "BOOT button short press");
            break;
        case ButtonEvent::Long:
            if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
            {
                displayBanner("WiFi reset");
                xSemaphoreGive(gDisplayMutex);
            }
            influxLogEvent("boot_button_long_wifi_reset");
            ESP_LOGW(TAG, "BOOT button long press -> clear WiFi credentials + reboot");
            portalClearWifiCredentials();
            vTaskDelay(pdMS_TO_TICKS(CFG_CONFIG_RESET_REBOOT_MS));
            ESP.restart();
            break;
        case ButtonEvent::Factory:
            if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
            {
                displayBanner("FACTORY");
                xSemaphoreGive(gDisplayMutex);
            }
            ESP_LOGW(TAG, "BOOT button very long press -> factory reset");
            vTaskDelay(pdMS_TO_TICKS(CFG_FACTORY_RESET_DELAY_MS));
            configFactoryReset();
            break;
        default:
            break;
        }
    }
}

static void scanTask(void *)
{
    // Stagger first scan so it doesn't pile onto boot traffic.
    vTaskDelay(pdMS_TO_TICKS(30000));

    while (true)
    {
        if (xSemaphoreTake(gWifiOpMutex, pdMS_TO_TICKS(CFG_WIFI_OP_LOCK_TIMEOUT_MS)) == pdTRUE)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                ESP_LOGI(TAG, "scan: starting passive WiFi scan");
                int found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
                if (found > 0)
                {
                    int count = min(found, CFG_SCAN_MAX_NETWORKS);
                    for (int i = 0; i < count; i++)
                    {
                        influxLogScanNetwork(
                            WiFi.SSID(i),
                            WiFi.BSSIDstr(i),
                            WiFi.RSSI(i),
                            WiFi.channel(i),
                            WiFi.SSID(i).length() == 0);
                    }
                    ESP_LOGI(TAG, "scan: logged %d/%d networks", count, found);
                }
                else
                {
                    ESP_LOGW(TAG, "scan: no networks found (err=%d)", found);
                }
                WiFi.scanDelete();
            }
            xSemaphoreGive(gWifiOpMutex);
        }
        else
        {
            ESP_LOGW(TAG, "scan: WiFi op busy, deferring scan");
        }
        vTaskDelay(pdMS_TO_TICKS(CFG_SCAN_INTERVAL_MS));
    }
}

static void displayBurnInTask(void *)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(CFG_DISPLAY_INVERT_PERIOD_MS));
        if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
        {
            displayToggleInversion();
            xSemaphoreGive(gDisplayMutex);
        }
    }
}

void setup()
{
    Serial.begin();

    vTaskDelay(pdMS_TO_TICKS(CFG_BOOT_SERIAL_SETTLE_MS));
    ESP_LOGI(TAG, "\n\n=== WiFi Logger %s booting ===", FW_VERSION);

    gDisplayMutex = xSemaphoreCreateMutex();
    gWifiOpMutex = xSemaphoreCreateMutex();
    if (!gDisplayMutex || !gWifiOpMutex)
    {
        ESP_LOGE(TAG, "mutex create failed");
        ESP.restart();
    }

    displayInit();
    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
    {
        displaySplash("Booting...", "v" FW_VERSION);
        xSemaphoreGive(gDisplayMutex);
    }

    configLoad();
    ESP_LOGI(TAG, "config: loaded=%d, label='%s', sample=%us, url='%s'",
             gConfig.loaded, gConfig.label.c_str(), gConfig.sampleSeconds,
             gConfig.influxUrl.c_str());

    if (!buttonBegin())
    {
        ESP_LOGE(TAG, "button: init failed");
    }

#if PIN_USER_LED >= 0
    pinMode(PIN_USER_LED, OUTPUT);
    setUserLed(false);
    xTaskCreate(ledTask, "led_task", CFG_TASK_LED_STACK, nullptr, CFG_TASK_LED_PRIO, nullptr);
#endif

    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
    {
        displaySplash("Connecting", "WiFi...");
        xSemaphoreGive(gDisplayMutex);
    }
    // begin wifi and set lower tx power
    WiFi.begin();
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_15dBm);

    bool connected = portalRunBlocking(); // blocks until WiFi up (or portal exits with creds)
    if (!connected)
    {
        if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
        {
            displaySplash("WiFi failed", "Rebooting...");
            xSemaphoreGive(gDisplayMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(CFG_WIFI_FAIL_REBOOT_MS));
        ESP.restart();
    }
    ESP_LOGI(TAG, "WiFi up: SSID=%s IP=%s gw=%s",
             WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(),
             WiFi.gatewayIP().toString().c_str());

    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
    {
        displaySplash("WiFi OK", WiFi.localIP().toString());
        xSemaphoreGive(gDisplayMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(CFG_WIFI_OK_SPLASH_MS));

    ntpBegin();
    // Wait briefly for NTP; not fatal if it doesn't sync — points will use
    // device-local time and be re-timestamped server-side if needed.
    for (int i = 0; i < CFG_NTP_SYNC_RETRIES && !ntpSynced(); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(CFG_NTP_SYNC_RETRY_MS));
    }
    ESP_LOGI(TAG, "NTP: synced=%d  time=%lu", ntpSynced(), (unsigned long)time(nullptr));

    influxBegin();
    ESP_LOGI(TAG, "Influx: began client, server=%s", gConfig.influxUrl.c_str());
    bool influxOk = influxValidate();
    ESP_LOGI(TAG, "Influx: validate=%d  err='%s'", influxOk, influxLastError().c_str());

    if (xSemaphoreTake(gDisplayMutex, pdMS_TO_TICKS(CFG_DISPLAY_LOCK_TIMEOUT_MS)) == pdTRUE)
    {
        displaySplash("Ready", gConfig.label);
        xSemaphoreGive(gDisplayMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(CFG_READY_SPLASH_MS));

    // Log a boot event so we can see device reboots on the Influx timeline.
    influxLogEvent("boot", String("fw=") + FW_VERSION);

    xTaskCreate(sampleTask, "sample_task", CFG_TASK_SAMPLE_STACK, nullptr, CFG_TASK_SAMPLE_PRIO, nullptr);
    xTaskCreate(influxProbeTask, "influx_probe", CFG_TASK_INFLUX_STACK, nullptr, CFG_TASK_INFLUX_PRIO, nullptr);
    xTaskCreate(buttonTask, "button_task", CFG_TASK_BUTTON_STACK, nullptr, CFG_TASK_BUTTON_PRIO, nullptr);
    xTaskCreate(displayBurnInTask, "display_burnin", CFG_TASK_DISPLAY_BURNIN_STACK, nullptr, CFG_TASK_DISPLAY_BURNIN_PRIO, nullptr);
    xTaskCreate(scanTask, "scan_task", CFG_TASK_SCAN_STACK, nullptr, CFG_TASK_SCAN_PRIO, nullptr);
}

void loop()
{
    // Nothing to do here — all tasks are FreeRTOS-based.
    vTaskDelete(nullptr);
}

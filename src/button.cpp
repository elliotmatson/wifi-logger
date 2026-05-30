#include "button.h"
#include "config.h"
#include "pins.h"
#include <Arduino.h>
#include <esp_log.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "button";

struct ButtonEdge
{
    bool pressed;
    TickType_t tick;
};

static QueueHandle_t gEdgeQueue = nullptr;
static QueueHandle_t gEventQueue = nullptr;

static void IRAM_ATTR buttonIsr()
{
    ButtonEdge edge;
    edge.pressed = (digitalRead(PIN_BOOT_BUTTON) == LOW);
    edge.tick = xTaskGetTickCountFromISR();

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(gEdgeQueue, &edge, &woken);
    if (woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void handleEdge(const ButtonEdge &edge, bool &pressed, TickType_t &pressStart, TickType_t &lastChange)
{
    ESP_LOGD(TAG, "edge: %s tick=%lu", edge.pressed ? "press" : "release", (unsigned long)edge.tick);
    if ((edge.tick - lastChange) < pdMS_TO_TICKS(CFG_BUTTON_DEBOUNCE_MS))
    {
        ESP_LOGD(TAG, "debounce skip");
        return;
    }
    if (edge.pressed == pressed)
    {
        ESP_LOGD(TAG, "duplicate state skip");
        return;
    }
    lastChange = edge.tick;
    pressed = edge.pressed;

    if (pressed)
    {
        pressStart = edge.tick;
        ESP_LOGI(TAG, "pressed");
        return;
    }

    uint32_t heldMs = (uint32_t)(edge.tick - pressStart) * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "released after %lu ms", (unsigned long)heldMs);
    ButtonEvent evt = ButtonEvent::None;
    if (heldMs >= CFG_BUTTON_FACTORY_MIN_MS)
        evt = ButtonEvent::Factory;
    else if (heldMs >= CFG_BUTTON_LONG_MIN_MS)
        evt = ButtonEvent::Long;
    else if (heldMs < CFG_BUTTON_SHORT_MAX_MS)
        evt = ButtonEvent::Short;

    if (evt != ButtonEvent::None)
    {
        ESP_LOGI(TAG, "event: %d", (int)evt);
        xQueueSend(gEventQueue, &evt, 0);
    }
}

static void buttonWorkerTask(void *)
{
    bool pressed = false;
    TickType_t pressStart = 0;
    TickType_t lastChange = 0;

    while (true)
    {
        ButtonEdge edge;
        if (xQueueReceive(gEdgeQueue, &edge, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        handleEdge(edge, pressed, pressStart, lastChange);
    }
}

bool buttonBegin()
{
    if (gEdgeQueue && gEventQueue)
    {
        return true;
    }

    gEdgeQueue = xQueueCreate(CFG_BUTTON_EDGE_QUEUE_LEN, sizeof(ButtonEdge));
    gEventQueue = xQueueCreate(CFG_BUTTON_EVENT_QUEUE_LEN, sizeof(ButtonEvent));
    if (!gEdgeQueue || !gEventQueue)
    {
        return false;
    }

    ESP_LOGI(TAG, "init: GPIO%d", PIN_BOOT_BUTTON);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    ESP_LOGI(TAG, "initial level: %d", digitalRead(PIN_BOOT_BUTTON));
    attachInterrupt(PIN_BOOT_BUTTON, buttonIsr, CHANGE);
    ESP_LOGI(TAG, "interrupt attached");

    if (xTaskCreate(buttonWorkerTask, "button_worker", CFG_BUTTON_TASK_STACK, nullptr, CFG_BUTTON_TASK_PRIO, nullptr) != pdPASS)
    {
        ESP_LOGE(TAG, "task create failed");
        return false;
    }
    ESP_LOGI(TAG, "ready");
    return true;
}

bool buttonGetEvent(ButtonEvent *outEvent, TickType_t waitTicks)
{
    if (!outEvent || !gEventQueue)
    {
        return false;
    }
    return xQueueReceive(gEventQueue, outEvent, waitTicks) == pdTRUE;
}

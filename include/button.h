#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

// Debounced BOOT button handler.
//   - Short press (<1.5 s)        → ButtonEvent::Short  (logged to Influx)
//   - Long press (>3 s, on release)→ ButtonEvent::Long   (clear WiFi creds + reboot)
//   - Very long press (>10 s)     → ButtonEvent::Factory (factory reset)

enum class ButtonEvent : uint8_t
{
    None,
    Short,
    Long,
    Factory
};

bool buttonBegin();
bool buttonGetEvent(ButtonEvent *outEvent, TickType_t waitTicks);

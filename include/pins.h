#pragma once
// ─── Hardware pinout: ESP32-C3 SuperMini 0.42" OLED ───────────────────────────
// Adjust these if you're on a different ESP32-C3 board.

// 0.42" SSD1306 OLED (72x40 visible, I2C)
#define PIN_I2C_SDA 5
#define PIN_I2C_SCL 6
#define OLED_I2C_ADDR 0x3C

// BOOT button — pulled high externally, pulls LOW when pressed.
// On this ESP32-C3 SuperMini variant this is GPIO9.
#define PIN_BOOT_BUTTON 9

// Onboard user LED (active LOW). Set to -1 if your board has none.
#define PIN_USER_LED 8
#define USER_LED_ACTIVE_LOW 1

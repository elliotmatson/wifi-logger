#include "display.h"
#include "config.h"
#include "pins.h"
#include <U8g2lib.h>
#include <Wire.h>
#include "qrcode.h"

// U8g2 has a dedicated constructor for the 72x40 "WIDE.HK" SSD1306 panel that
// these ESP32-C3 0.42" boards use. It internally applies the +30 column and
// +0 page offset needed to map the visible window onto the controller's RAM.
static U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ----- internal state for banner overlay --------------------------------------
static String bannerText;
static uint32_t bannerExpiresAt = 0;
static int lastQrSize = 0;
static bool lastQrOk = false;
static constexpr int kPortalQrYOffset = 13;
static bool gDisplayInverted = false;

static void applyDisplayInvert(bool invert)
{
    u8g2.sendF("c", invert ? 0xA7 : 0xA6); // SSD1306: inverse/normal display
}

static void drawQrCode(esp_qrcode_handle_t qrcode)
{
    lastQrSize = esp_qrcode_get_size(qrcode);
    for (int y = 0; y < lastQrSize; ++y)
    {
        for (int x = 0; x < lastQrSize; ++x)
        {
            if (esp_qrcode_get_module(qrcode, x, y))
            {
                u8g2.drawPixel(x, kPortalQrYOffset + y);
            }
        }
    }
}

static void drawBannerIfActive()
{
    if (bannerText.length() == 0 || millis() > bannerExpiresAt)
        return;
    // Invert a strip at the top with the banner text
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 72, 10);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(1, 8);
    u8g2.print(bannerText);
    u8g2.setDrawColor(1);
}

void displayInit()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    u8g2.setI2CAddress(OLED_I2C_ADDR << 1); // U8g2 wants the 8-bit form
    u8g2.begin();
    u8g2.setBusClock(400000);
    u8g2.setContrast(1);
    gDisplayInverted = false;
    applyDisplayInvert(false);
    u8g2.clearBuffer();
    u8g2.sendBuffer();
}

void displaySplash(const String &line1, const String &line2)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, 10);
    u8g2.print("WiFi Logger");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(0, 22);
    u8g2.print(line1);
    if (line2.length())
    {
        u8g2.setCursor(0, 32);
        u8g2.print(line2);
    }
    u8g2.sendBuffer();
}

void displayPortal(const String &apSsid, const String &apIp)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(0, 8);
    u8g2.print("SETUP MODE");
    u8g2.drawHLine(0, 10, 72);

    // QR code: left, 36x36, shifted down to avoid line (y=7)
    String payload = "WIFI:S:" + apSsid + ";;";
    esp_qrcode_config_t qrCfg = ESP_QRCODE_CONFIG_DEFAULT();
    qrCfg.display_func = drawQrCode;
    qrCfg.max_qrcode_version = 4;
    qrCfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    lastQrSize = 0;
    lastQrOk = (esp_qrcode_generate(&qrCfg, payload.c_str()) == ESP_OK);
    int qrsize = lastQrSize;
    int qr_x = 0, qr_y = kPortalQrYOffset;

    if (!lastQrOk || qrsize <= 0)
    {
        u8g2.setCursor(qr_x, qr_y + 10);
        u8g2.print("QR ERR");
    }

    // SSID: right of QR, vertically centered relative to new QR position
    int ssid_x = qr_x + qrsize + 2;
    int ssid_y = qr_y + 13;

    int w = u8g2.getStrWidth(chipIdSuffix().c_str());
    u8g2.setCursor(ssid_x + (72 - ssid_x - w) / 2, ssid_y + 7);
    u8g2.print(chipIdSuffix());

    u8g2.sendBuffer();
}

void displayStatus(const DisplayStatus &s)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);

    // Row 1: label (left), wifi/influx state indicators (right)
    u8g2.setCursor(0, 7);
    String lbl = s.label;
    if (lbl.length() > 10)
        lbl = lbl.substring(0, 10);
    u8g2.print(lbl);
    // Right-aligned mini icons: W=wifi, I=influx; invert if down
    int rightX = 72 - 12;
    if (!s.wifiUp)
    {
        u8g2.drawBox(rightX, 0, 6, 9);
        u8g2.setDrawColor(0);
    }
    u8g2.setCursor(rightX + 1, 7);
    u8g2.print('W');
    u8g2.setDrawColor(1);
    if (!s.influxUp)
    {
        u8g2.drawBox(rightX + 6, 0, 6, 9);
        u8g2.setDrawColor(0);
    }
    u8g2.setCursor(rightX + 7, 7);
    u8g2.print('I');
    u8g2.setDrawColor(1);
    u8g2.drawHLine(0, 9, 72);

    // Row 2: SSID + RSSI
    u8g2.setCursor(0, 18);
    String ssid = s.ssid.length() ? s.ssid : String("--");
    if (ssid.length() > 9)
        ssid = ssid.substring(0, 9);
    u8g2.print(ssid);
    if (s.wifiUp)
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%ddBm", s.rssi);
        int w = u8g2.getStrWidth(buf);
        u8g2.setCursor(72 - w, 18);
        u8g2.print(buf);
    }

    // Row 3: IP
    u8g2.setCursor(0, 27);
    u8g2.print(s.ip.length() ? s.ip : String("0.0.0.0"));

    // Row 4: ping times — "gw:12 dns:24"
    u8g2.setCursor(0, 36);
    char p[24];
    auto fmt = [](char *dst, size_t n, const char *k, int ms)
    {
        if (ms < 0)
            snprintf(dst, n, "%s:-- ", k);
        else if (ms > 999)
            snprintf(dst, n, "%s:>1s ", k);
        else
            snprintf(dst, n, "%s:%d ", k, ms);
    };
    char a[12], b[12];
    fmt(a, sizeof(a), "gw", s.pingGw);
    fmt(b, sizeof(b), "dns", s.pingDns);
    snprintf(p, sizeof(p), "%s%s", a, b);
    u8g2.print(p);

    // Buffer indicator: a tiny "B:N" if any points buffered
    if (s.bufferedPoints > 0)
    {
        char bb[10];
        snprintf(bb, sizeof(bb), "B:%u", s.bufferedPoints);
        int w = u8g2.getStrWidth(bb);
        u8g2.setCursor(72 - w, 36);
        // overwrite right side
        u8g2.setDrawColor(0);
        u8g2.drawBox(72 - w - 1, 29, w + 1, 8);
        u8g2.setDrawColor(1);
        u8g2.setCursor(72 - w, 36);
        u8g2.print(bb);
    }

    drawBannerIfActive();
    u8g2.sendBuffer();
}

void displayBanner(const String &text)
{
    bannerText = text;
    bannerExpiresAt = millis() + 1500;
}

void displayToggleInversion()
{
    gDisplayInverted = !gDisplayInverted;
    applyDisplayInvert(gDisplayInverted);
}

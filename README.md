# WiFi Connectivity Logger — ESP32-C3 + 0.42" OLED

> AI assistance disclaimer: Large portions of this project (including code and
> documentation) were created with AI assistance and then reviewed/edited by me.
> Validate behavior, security, and production readiness before use. I needed something quick and simple
> to get visibility into WiFi issues in our buildings, and this was good enough. It’s not a polished product,
> but it’s a solid starting point if you want to build something similar.

A small fleet of ESP32-C3 boards that sit in your buildings, watch the WiFi,
and stream connectivity data to InfluxDB so you can see exactly when and where
things get flaky.

## What it does

Every N seconds (default 30) each device records:

- Connected SSID
- Connected BSSID
- Signal strength (RSSI, dBm)
- WiFi channel
- Local IP + gateway IP
- Round-trip ping to the default gateway, 8.8.8.8, and 1.1.1.1
- Free heap and uptime (handy for catching firmware issues)

It also runs periodic passive scans and records nearby APs to a separate
measurement (`wifi_scan`), including SSID, BSSID, RSSI, channel, and whether
the SSID is hidden.

It also logs **events** for:

- Power-on / reboot
- BOOT button press (short = quick mark, long = clear WiFi credentials + reboot,
  very long = factory reset)
- WiFi lifecycle transitions (connected, disconnected, roaming to new BSSID)

Points are batched and **buffered in RAM** when InfluxDB or WiFi is
unreachable, then drained automatically on reconnect. Every point is
timestamped at sample time, not at send time.

A 0.42" OLED on the device shows live status (label, SSID, RSSI, IP, ping,
buffer depth) and the AP-config SSID during setup.

## Hardware

Recommended: **ESP32-C3 SuperMini 0.42" OLED** boards. Default pinout used:

| Function     | GPIO |
| ------------ | ---- |
| OLED I²C SDA | 5    |
| OLED I²C SCL | 6    |
| OLED address | 0x3C |
| BOOT button  | 9    |
| User LED     | 8    |

If you're on a different ESP32-C3 board, edit `src/pins.h`.

## Building

1. Install [PlatformIO](https://platformio.org/install) (VS Code extension
   or `pipx install platformio`).

2. Build & flash:

   ```bash
   pio run -t upload
   pio device monitor
   ```

## First-boot setup

By default, the firmware tries to auto-connect with any saved/preloaded WiFi
credentials. If Influx is not configured yet (or WiFi can't be joined), it
opens the captive portal in **AP mode** with SSID `WiFiLogger-XXXXXX`
(last 6 hex of the chip MAC). Fill in:

| Field                | Example                               |
| -------------------- | ------------------------------------- |
| WiFi SSID / password | (your building WiFi)                  |
| Device label         | `Sanctuary-NE-Corner`                 |
| InfluxDB URL         | `https://influx.example.org`          |
| InfluxDB org         | `grace-bible`                         |
| InfluxDB bucket      | `wifi_logs`                           |
| InfluxDB API token   | (write-enabled token for that bucket) |
| Sample interval (s)  | `30`                                  |
| Ping target 1        | `8.8.8.8`                             |
| Ping target 2        | `1.1.1.1`                             |

Save — the device reboots, joins your WiFi, and starts logging.

## Re-configuring without re-flashing

- **Hold BOOT for ~3 s** → clear stored WiFi credentials and reboot.
- **Hold BOOT for ~10 s** → factory reset (clears app config + WiFi creds).

## InfluxDB schema

Measurement: `wifi_connectivity`

| kind  | name                 | example                  |
| ----- | -------------------- | ------------------------ |
| tag   | `device`             | `Auditorium-NE-Corner`   |
| tag   | `chip_id`            | `1A2B3C`                 |
| tag   | `ssid`               | `Staff-Network`          |
| field | `rssi`               | `-62` (dBm)              |
| field | `channel`            | `6`                      |
| field | `bssid`              | `B4:FB:E4:11:22:33`      |
| field | `ip`                 | `10.10.20.84`            |
| field | `gateway`            | `10.10.20.1`             |
| field | `ping_gateway_ms`    | `4` (`-1` = packet lost) |
| field | `ping_google_ms`     | `21`                     |
| field | `ping_cloudflare_ms` | `19`                     |
| field | `ping_gw_ok`         | `true` / `false`         |
| field | `ping_google_ok`     | `true` / `false`         |
| field | `ping_cloudflare_ok` | `true` / `false`         |
| field | `uptime_s`           | `83214`                  |
| field | `free_heap`          | `184320`                 |

Measurement: `wifi_event` — tag `event` includes:

- `boot`
- `boot_button_short`
- `boot_button_long_wifi_reset`
- `wifi_connected`
- `wifi_disconnected`
- `wifi_roam`

Measurement: `wifi_scan`

| kind  | name      | example                |
| ----- | --------- | ---------------------- |
| tag   | `device`  | `Auditorium-NE-Corner` |
| tag   | `chip_id` | `1A2B3C`               |
| tag   | `ssid`    | `Staff-Network`        |
| tag   | `bssid`   | `B4:FB:E4:11:22:33`    |
| field | `rssi`    | `-67`                  |
| field | `channel` | `6`                    |
| field | `hidden`  | `true` / `false`       |

### Useful Flux query (per-AP packet loss, 5 min buckets)

```flux
from(bucket: "wifi_logs")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "wifi_connectivity"
                   and (r._field == "ping_gw_ok"
                     or r._field == "ping_google_ok"
                     or r._field == "ping_cloudflare_ok"))
  |> aggregateWindow(every: 5m, fn: mean)
  |> map(fn: (r) => ({r with _value: (1.0 - r._value) * 100.0}))
  |> group(columns: ["device", "_field"])
```

## Project layout

```
wifi-logger/
├── platformio.ini       # PIO config, ESP32-C3 toolchain, library deps
├── src/
│   ├── main.cpp         # boot flow + main loop
│   ├── pins.h           # GPIO assignments
│   ├── config.cpp       # NVS-backed runtime config (Preferences)
│   ├── secrets.h        # local-only defaults (gitignored)
│   ├── display.cpp      # 72x40 SSD1306 OLED (U8g2)
│   ├── sampler.cpp      # WiFi metrics + ping
│   ├── influx.cpp       # InfluxDB v2 client + retry buffer
│   ├── portal.cpp       # WiFiManager AP captive portal w/ custom fields
│   └── button.cpp       # debounced BOOT button state machine
└── lib/
```

## Notes / gotchas

- **TLS to Influx Cloud / Let's Encrypt:** ESP32 needs a CA bundle for
  proper cert validation. This firmware uses ESP-IDF `esp_http_client` with
  the ESP CRT bundle attached (`esp_crt_bundle_attach`), so public CAs (e.g.
  Let's Encrypt) should work out of the box.
- **NTP:** Influx points are timestamped in **seconds** since epoch. If NTP
  hasn't synced yet, points are sent without an explicit timestamp and Influx
  will stamp them at ingest time. The code waits briefly during boot for sync;
  if your network blocks outbound NTP (UDP 123), set `ntpServer` to an
  internal time source in the portal.
- **Cardinality:** the SSID is a tag because that's how you'll typically
  group dashboards. `wifi_connectivity` keeps BSSID as a _field_ to control
  series growth, while `wifi_scan` stores BSSID as a tag for per-AP scan views.

## License

MIT.

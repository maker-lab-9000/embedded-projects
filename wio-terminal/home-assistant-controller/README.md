# Wio Terminal Home Assistant controller

A Seeed Wio Terminal that shows Home Assistant dashboards. Joystick
left/right switches pages. A battery meter sits in the top-left corner when
the battery chassis is attached. Backlight runs at 35 %.

**Tram**: when the next M1 leaves Kuckhoffstr. (green tram), when the last
one left (red tram), and a 24 h chart of the minutes between trams.

![Tram page](docs/images/tram-page.png)

**Climate**: temperature and humidity per room, then air quality, with the
Control Panel dashboard's colour rules:

| Room | Temperature | Humidity |
|---|---|---|
| Bedroom | `sensor.multi_sensor_bedzimmer` | `sensor.multi_sensor_bedzimmer_2` |
| Theo's room | `sensor.babyroom` | `sensor.babyroom_2` |
| Bathroom | `sensor.temperature_9` | `sensor.humidity_10` |
| Living room | `sensor.living_room` | `sensor.living_room_2` |

- Thermometer: blue below 20 °C, yellow 20–26 °C, red above 26 °C.
- Droplet: green up to 65 %, yellow above 65 %, red above 75 %.
- PM2.5 (`sensor.nova_pm2_5`, limit 10) and PM10 (`sensor.nova_pm10`,
  limit 20): green below the limit, yellow up to twice it, red above.
- Grey icon and `--`: sensor unavailable.

![Climate page](docs/images/climate-page.png)

Planned pages: Lights (brightness and color), Plugs, Status. Graphs are
deferred.

## Buttons

- **C** (leftmost top button): display off / on (35 % brightness). The connection stays up while dark.
- **A**, **B**, joystick: wake the display when it is off.

## Setup

1. Copy `firmware/m1_tram/secrets.h.example` to `firmware/m1_tram/secrets.h`
   and fill in WiFi and a Home Assistant long-lived access token
   (Profile → Security). `secrets.h` and `.env` are gitignored.
2. Libraries (Arduino library folder): `ArduinoJson` 7.x, `Seeed Arduino rpcWiFi`,
   `Seeed Arduino rpcUnified`, `SparkFun BQ27441 LiPo Fuel Gauge`. The display uses the Seeed SAMD core's bundled `TFT_eSPI`.
3. The RTL8720 WiFi chip needs Seeed's eRPC firmware (tested with 2.1.3).

```sh
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal firmware/m1_tram
arduino-cli upload  --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodem14101 firmware/m1_tram
```

Host-side unit tests (time parsing, HA message handling, chart math, WiFi AP
choice, websocket framing):

```sh
test/run.sh
```

## How it works

- One websocket to `ws://<HA_HOST>:8123/api/websocket`: `subscribe_entities`
  for the tram and climate entities (live pushes), `history/history_during_period`
  for the chart every 10 min, a ping every 30 s.
- If the token is rejected, the device stops and shows "Token rejected" —
  no retries, because Home Assistant bans IPs after repeated failed logins.
  Network drops reconnect with backoff (5 s doubling to 5 min).
- WiFi: the home SSID has a 5 GHz AP whose security the RTL8720 doesn't
  support (a plain `WiFi.begin()` hangs there), so the firmware scans and
  joins the strongest WPA/WPA2 AP by BSSID (`wifi_pick.h`).

## Watchdog

If the main loop stalls for 8 s, the watchdog's early-warning interrupt holds
the RTL8720 WiFi chip in reset, and at 16 s the board resets. This catches the
rpcWiFi connect that hangs for minutes when the HA host is down. The WiFi chip
needs the long reset: after a short one its scans came back empty and hung.
The last step reached before a reset is kept in backup RAM and printed at boot.

## Serial commands (115200 baud)

- `r`: drop and reconnect the Home Assistant session (tests recovery).
- `p`: next page (like joystick right).
- `s`: dump a screenshot: `SHOT\n` followed by 320×240 RGB565 pixels, big-endian.
- `c`: uptime, HA status, watchdog early-warning count.
- `w`: hang on purpose to test the watchdog.

Opening and closing the serial port repeatedly while the board is running can
make an rpcWiFi call hang (then the watchdog resets it). Keep one monitor open,
or check resets passively by watching whether `/dev/cu.usbmodem*` disappears.

## Files

| File | Role |
|---|---|
| `m1_tram.ino` | setup/loop, page table |
| `net.h` | WiFi with backoff, NTP clock |
| `wifi_pick.h` | which AP to join (pure) |
| `ws_client.h`, `ws_frame.h` | minimal websocket client (ArduinoWebsockets has no SAMD support) |
| `ha.h`, `ha_proto.h` | HA session I/O, protocol handling (pure) |
| `tram_model.h`, `climate_model.h` | HA messages → page state, colour rules (pure) |
| `timeutil.h` | ISO time parsing, Berlin time, "in 4 min" / "3 min ago" (pure) |
| `header.h` | shared header: battery, title, HA status, clock; colours |
| `page_tram.h`, `chart.h` | tram page drawing, chart geometry (pure) |
| `page_climate.h` | climate page drawing |
| `ui.h`, `sleep_ctl.h` | buttons, display sleep, page switching |
| `backlight.h` | PWM brightness via TC0 → CCL → EVSYS (the pin has no PWM) |
| `battery.h`, `battery_meter.h` | BQ27441 fuel gauge, meter math (pure) |
| `wdt.h` | watchdog, WiFi chip reset on early warning, breadcrumbs |

Design: `docs/superpowers/specs/2026-11-01-tram-page-design.md`.
Plan: `docs/superpowers/plans/2026-11-01-tram-page.md`.

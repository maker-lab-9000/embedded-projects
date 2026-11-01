# Wio Terminal Home Assistant controller

A Seeed Wio Terminal that shows Home Assistant dashboards. Version 1 has one
page, **Tram**: the next M1 from Kuckhoffstr. with a live countdown, a 24 h
chart of the minutes between trams, and when the last M1 left.

![Tram page](docs/images/tram-page.png)

Planned pages (joystick left/right): Lights (brightness and color), Climate,
Plugs, Status. Graphs are deferred.

## Buttons

- **C** (leftmost top button): display off / on. The connection stays up while dark.
- **A**, **B**, joystick: wake the display when it is off.

## Setup

1. Copy `firmware/m1_tram/secrets.h.example` to `firmware/m1_tram/secrets.h`
   and fill in WiFi and a Home Assistant long-lived access token
   (Profile → Security). `secrets.h` and `.env` are gitignored.
2. Libraries (Arduino library folder): `ArduinoJson` 7.x, `Seeed Arduino rpcWiFi`,
   `Seeed Arduino rpcUnified`. The display uses the Seeed SAMD core's bundled `TFT_eSPI`.
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
  for the three tram entities (live pushes), `history/history_during_period`
  for the chart every 10 min, a ping every 30 s.
- If the token is rejected, the device stops and shows "Token rejected" —
  no retries, because Home Assistant bans IPs after repeated failed logins.
  Network drops reconnect with backoff (5 s doubling to 5 min).
- WiFi: the home SSID has a 5 GHz AP whose security the RTL8720 doesn't
  support (a plain `WiFi.begin()` hangs there), so the firmware scans and
  joins the strongest WPA/WPA2 AP by BSSID (`wifi_pick.h`).

## Serial commands (115200 baud)

- `r`: drop and reconnect the Home Assistant session (tests recovery).
- `s`: dump a screenshot: `SHOT\n` followed by 320×240 RGB565 pixels, big-endian.

## Files

| File | Role |
|---|---|
| `m1_tram.ino` | setup/loop, page table |
| `net.h` | WiFi with backoff, NTP clock |
| `wifi_pick.h` | which AP to join (pure) |
| `ws_client.h`, `ws_frame.h` | minimal websocket client (ArduinoWebsockets has no SAMD support) |
| `ha.h`, `ha_proto.h` | HA session I/O, protocol handling (pure) |
| `tram_model.h` | HA messages → tram state (pure) |
| `timeutil.h` | ISO time parsing, Berlin time, "in 4 min" / "3 min ago" (pure) |
| `page_tram.h`, `chart.h` | drawing, chart geometry (pure) |
| `ui.h`, `sleep_ctl.h` | buttons, backlight, page switching |

Design: `docs/superpowers/specs/2026-11-01-tram-page-design.md`.
Plan: `docs/superpowers/plans/2026-11-01-tram-page.md`.

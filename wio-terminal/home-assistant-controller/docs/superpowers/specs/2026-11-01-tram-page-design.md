# Wio Terminal Home Assistant controller: tram page (v1)

Date: 2026-11-01. Status: approved in chat ("go"), written up for planning.

## Intent

The user wants a Wio Terminal on the desk that shows Home Assistant
dashboards and controls, flipped through with the joystick. The long-term
page order is **Tram → Lights → Climate → Plugs → Status** (Graphs page
deferred, to be discussed later). This first version builds only the
**Tram** page, which answers one question at a glance: *when is the next M1
from Kuckhoffstr., and how often are trams running?*, without opening the
Home Assistant app.

v1 must be structured so the later pages (including light control with
brightness/color) slot in without a rewrite: a page table, a shared
Home Assistant connection with per-entity state, and joystick navigation
hooks.

## Goals

- Show the Tram page reproducing the user's three HA cards (screenshot
  2026-11-01): Next tram, Minutes between trams (24 h chart), Last M1
  departure.
- Live updates pushed from Home Assistant (no polling of states).
- Button C toggles the display off/on (backlight off); A, B or any
  joystick input also wakes it. HA connection stays up while dark.
- Survive WiFi / HA outages: keep last known data on screen, show a
  connection indicator, reconnect on its own.

## Non-goals (v1)

- Lights, Climate, Plugs, Status, Graphs pages.
- Controlling anything (no service calls yet).
- Reading the HA Lovelace dashboard config; the page is defined in code.
- TLS (HA is on plain http on the LAN).
- Battery / power saving beyond the backlight.

## Anti-goals

- **Never hammer HA with failed auth.** HA's IP ban already triggered
  once in this project. On an `auth_invalid` reply the device must stop
  retrying entirely (show "Token rejected" on screen) until reset.
  Network-level reconnects use exponential backoff (5 s doubling to a
  5 min cap).
- No secrets in git: token and WiFi credentials live in a gitignored
  `secrets.h` (`.env` is also gitignored).
- Don't block `loop()` for long: button presses and the countdown must
  stay responsive while the network is slow.

## Hardware and toolchain

- Seeed Wio Terminal (SAMD51, 192 KB RAM, 320×240 TFT, no touch,
  RTL8720DN WiFi co-processor via eRPC). Connected at
  `/dev/cu.usbmodem14101`.
- Inputs: top buttons `WIO_KEY_A/B/C` (C is the leftmost button),
  5-way joystick `WIO_5S_*`. Backlight: `LCD_BACKLIGHT` pin.
- Built with `arduino-cli`, FQBN `Seeeduino:samd:seeed_wio_terminal`,
  same as the sibling `orbital-density` project. Installed libraries:
  Seeed Arduino rpcWiFi 1.1.0, rpcUnified 2.1.4, ArduinoWebsockets 0.5.4,
  ArduinoJson 7.4.3, Seeed_GFX / bundled TFT_eSPI.
- Sketch at `firmware/m1_tram/` with header-only modules next to it
  (house style from `orbital-density`: `xxxInit()`, non-blocking
  `xxxPoll()`).

## Home Assistant side

Instance `http://192.168.178.137:8123`, long-lived token (verified
working 2026-11-01). Entities:

| Card | Entity | Shape |
|---|---|---|
| Next tram | `sensor.kuckhoffstr_berlin` | state is a string (`"Next M1 at 22:58"` or `"N/A"`); attribute `departures` is a list of `{line_name, line_type, time "HH:MM", timestamp ISO-8601 with offset, direction, color "#D82020", delay seconds, cancelled bool, ...}` (shape confirmed on sibling stops of the same integration; Kuckhoffstr list was empty at check time) |
| Minutes between trams | `input_number.home_m1_tram_minutes_between_trams` | numeric state, unit `min`; changes a few times per day; no long-term statistics, so the chart uses state history |
| Last M1 departure | `sensor.home_m1_tram_last_departed` | `device_class: timestamp`, state ISO-8601 UTC |

Connection: one WebSocket to `ws://192.168.178.137:8123/api/websocket`.

1. Auth handshake (`auth_required` → `auth` with token → `auth_ok`).
2. `subscribe_entities` with `entity_ids` = the three above. Handle the
   compressed format: initial `{"a": {id: {"s", "a", "lc", ...}}}` and
   changes `{"c": {id: {"+": {"s"?, "a"?, "lc"?}}}}`. A changed
   attribute arrives with its full new value.
3. `history/history_during_period` for the input_number, last 24 h,
   `minimal_response: true`, `no_attributes: true`, on connect and every
   10 minutes. Keep at most ~96 points (step data; if more arrive, keep
   the latest by time).

Use ArduinoJson filters so only the needed fields are kept; keep at most
the first 3 non-cancelled departures.

## Screen (320×240, dark theme like HA)

```
┌────────────────────────────────────────┐
│ Kuckhoffstr.                 ● 22:57   │  ← stop, connection dot, clock
│ ┌──┐                                   │
│ │M1│ → Schillerstr.          in 4 min  │  ← large countdown
│ └──┘ then 22:59, 23:19                 │
│────────────────────────────────────────│
│ Minutes between trams          20 min  │
│ 30┤ ┌┐ ┌──┐                           │
│ 20┤─┘└─┘  └───────────────────────     │  ← step chart, last 24 h
│ 10┤                                    │
│   0h     6h     12h     18h    now     │
│────────────────────────────────────────│
│ 🚋 Last M1 departed      3 min ago     │
└────────────────────────────────────────┘
```

- Next tram: line badge in the departure's `color`; direction text;
  countdown "in N min" computed from `timestamp` minus now (≤0 → "now");
  delay > 0 → countdown in orange with "+N". Next two departure times
  from `time`. Empty list → "No trams scheduled".
- Chart: step line, x = now−24 h … now, y auto-scaled with some padding,
  current value at top right.
- Last departed: relative time ("x sec/min/h ago"), refreshed every
  second on-device.
- Connection dot: green = HA connected, yellow = connecting, red = no
  WiFi / auth rejected (with short text when not green).
- Redraw only regions whose data changed (avoid full-screen flicker).

## Time

NTP via WiFi UDP once connected, resync hourly. Countdown math uses epoch
seconds from the ISO timestamps (offset-aware), so it doesn't depend on
the local zone. The header clock shows Berlin time (CET/CEST, EU DST
rule computed in code).

## Builder's discretion

Module boundaries, redraw strategy, JSON buffer sizes, fonts, exact
colors, reconnect timer details, how `secrets.h` is templated
(`secrets.h.example` committed).

## Known risks (verify first)

- The RTL8720DN must run Seeed's eRPC firmware for rpcWiFi to work; if
  WiFi init fails, the firmware needs flashing (`ambd_flash_tool`).
- ArduinoWebsockets on SAMD/rpcWiFi is not a documented combination;
  first task is a bring-up sketch that authenticates to HA over
  WebSocket. Fallback: a minimal hand-written WebSocket client over
  `WiFiClient` (text frames, client masking, ping/pong).
- Initial `subscribe_entities` payload and history reply size vs RAM.

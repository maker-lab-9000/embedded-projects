# M5StickS3 WiFi Presence Scanner — Design

**Date:** 2026-08-18
**Status:** approved design, pending implementation plan

## Goal

A standalone, portable "device radar" on the M5StickS3: passively listen to
2.4 GHz WiFi traffic and show, on the built-in LCD, how many active devices
(phones, laptops) are around and how close — an ambient presence/busyness
meter that works anywhere, on battery, with no network connection.

The stick is repurposed from soil monitoring: the soil firmware and
calibration remain in the repo (`m5sticks3/soil-moisture-monitor`), the
`/soil.csv` log survives on the LittleFS partition across app flashes, and
switching back is a single `arduino-cli upload`.

## Non-goals

- No CSI/channel-state motion sensing (that needs an AP anchor; rejected for
  portability).
- No Home Assistant, MQTT, ESPHome, or any cloud/network reporting.
- No packet transmission of any kind — receive-only. No deauth, no probing,
  no injection.
- No logging or persistence: MACs live in RAM for the sliding window only;
  nothing is written to flash or displayed per-device. Aggregates only.

## Sensing design

- **Radio:** ESP32 promiscuous (monitor) mode via `esp_wifi_set_promiscuous`
  with a receive callback; filter to management + data frames. WiFi station
  mode initialized but never connected.
- **Channel hopping:** cycle channels 1–13, 250 ms dwell
  (`esp_wifi_set_channel` from `loop()`), full sweep ≈ 3.3 s.
- **Frame parsing in the callback:**
  - Beacon / probe-response → transmitter is an **AP**.
  - Probe request → transmitter is a **prober** (client actively scanning;
    MAC usually randomized per burst — counted but known-fuzzy).
  - Data frames → transmitter and receiver addresses that are not known APs
    and not broadcast/multicast are **clients** (stable MACs, the reliable
    signal).
  - Every entry records RSSI (exponential moving average, alpha 0.3).
- **Device table:** fixed array of 128 entries `{mac[6], lastSeenMs, rssiEma,
  kind}`, evict oldest when full. Sliding window: entries older than 60 s are
  expired. The callback runs in the WiFi task; all table access is guarded by
  a `portMUX_TYPE` critical section, and the callback does bounded work only.
- **Proximity buckets** by smoothed RSSI: near ≥ −55 dBm, mid −55…−75 dBm,
  far < −75 dBm.
- **Headline metric:** count of client + prober devices with RSSI ≥ −75 dBm
  ("nearby devices" — same room / few walls). Total devices and AP count
  shown separately.
- **Honesty note (documented in README):** MAC randomization inflates prober
  counts and double-counts returning phones; the number is an activity
  meter, not a census. Client (data-frame) devices are the trustworthy part.

## Display (240 × 135 landscape, same visual language as the monitors)

```
WiFi Radar        ch 6      header (size 2) + live hop channel (top right)
   5 near                   big count (size 4): nearby devices, color-scaled
  12 devs    8 APs          window totals (size 2, right column layout)
[ sparkline, last hour ]    nearby-count history, 15 s resolution
fps 42   bat 77%  up 0:12   frames/sec, battery, uptime (size 1)
```

- Big-count colors: 0 dark grey · 1–3 green · 4–9 yellow · ≥ 10 red.
- Sparkline: 240 samples × 15 s = last 60 minutes of the nearby count, cyan,
  auto-scaled y-axis (0 to max in window, min headroom 5), grey border box —
  coordinates mirror the soil monitor's box (2, 64, 236, 44).
- Layout coordinates follow `m2_monitor`: header y0, big count (0, 20)
  size 4, right column x 104 (y 24 / y 44), diag row y 124 size 1.
- Refresh: counts and diag row 1 Hz; sparkline commits one sample per 15 s;
  channel indicator updates on hop.

## Controls

- **KEY1 click** — screen off/on (scanning continues; backlight dominates
  battery draw). Same `M5.Display.sleep()/wakeup()` + full-redraw pattern and
  `screenOn` draw-guards as the soil monitor.
- **KEY2 hold 3 s** — clear the device table and sparkline history, chirp
  (same two-tone chirp via `M5.Speaker`).
- Side button remains system reset / download mode, untouched.

## Repo layout

```
m5sticks3/wifi-presence-scanner/
  README.md            what it shows, how to read it, privacy notes, build
  firmware/m1_sniff/   serial proof: per-channel frame counts, RSSI, MAC kinds
  firmware/m2_radar/   full radar UI
```

Root `README.md` project table gets a row. `.gitignore` copied from the
sibling project.

## Toolchain

Identical to the soil monitor: `esp32:esp32` core, FQBN
`esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB`,
M5Unified for display/buttons/speaker/battery. Promiscuous APIs
(`esp_wifi_set_promiscuous`, `esp_wifi_set_channel`, RX callback types) come
from `esp_wifi.h`, available in the Arduino core. Serial quirks of this board
(port-open resets the chip; flashing parks it in download mode — single-click
to boot) are inherited and documented.

## Validation plan

1. **m1_sniff:** serial prints a per-sweep summary (channel → frames, unique
   MACs by kind, strongest RSSI). Pass: beacons visible on the home AP's
   channel, frame counts react when a phone nearby actively browses, RSSI
   rises as the phone approaches the stick.
2. **m2_radar:** big count increases when a phone is brought next to the
   stick and used; count decays within ~60 s of the phone leaving/idling;
   AP count plausible for the flat; sparkline draws; KEY1 toggles screen with
   scanning confirmed alive (counts changed while dark); KEY2 reset chirps
   and zeroes the window; battery field shows.
3. User walks one room away with the stick: AP count changes with location,
   demonstrating portability.

## Risks / accepted trade-offs

- **Count inflation from MAC randomization** — accepted; documented; the
  display favors the stable client count in the headline.
- **Busy environments** (cafés: hundreds of devices) — table capped at 128,
  oldest evicted; the fps counter makes saturation visible. Accepted for v1.
- **Callback CPU load** at high frame rates — callback work is bounded
  (parse + table upsert under critical section); if it proves too heavy the
  fallback is a queue drained in `loop()`.
- **Battery** ≈ 2–3 h with radio always receiving (screen off stretches it);
  it's a walk-around toy, not an all-day logger. Accepted.

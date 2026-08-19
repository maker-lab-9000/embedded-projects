# WiFi Presence Scanner (M5StickS3)

A standalone, portable "device radar" on the M5StickS3: it passively listens to
2.4 GHz WiFi traffic and shows, on the built-in LCD, how many active devices
(phones, laptops) are around and how close — an ambient presence/busyness
meter. No network connection, no cloud, works anywhere on battery.

**Maintainer:** George Babanau · actively maintained (Aug 2026)

## What it measures

The radio runs in promiscuous (monitor) mode — **receive-only**, never
connected to any network, never transmitting. It hops across channels 1–13
(~250 ms each, a full sweep every ~3.3 s) and classifies every frame it hears:

- **Beacons / probe responses** → the sender is an **access point** (AP).
- **Probe requests** → the sender is a **client actively scanning** for
  networks (a "prober").
- **Data frames** → the sender is an **associated client** (phone, laptop).

Each unique device (by MAC) is tracked in a 60-second sliding window with a
smoothed RSSI (signal strength). The headline number is **nearby devices** —
non-AP devices with RSSI ≥ −75 dBm, i.e. same room or a wall away.

**Honesty note:** modern phones randomize their MAC address while probing, so
the count is an *activity/busyness meter, not a head count* — a single phone
can appear as several probers, and a returning device may be counted anew.
The data-frame client count (stable MACs) is the trustworthy part; treat the
"near" number as "how much active radio is close to me" rather than an exact
number of people.

## Hardware

- M5StickS3 (ESP32-S3-PICO-1-N8R8 — 8 MB flash, 1.14" 135×240 LCD,
  250 mAh battery). No sensor or wiring needed — the radio *is* the sensor.
- 2.4 GHz only (the ESP32-S3 has no 5 GHz radio), single antenna.

## Firmware

| Sketch | Purpose |
|--------|---------|
| `firmware/m1_sniff`  | serial-only sniffer: per-channel frame counts, device kinds, RSSI (bring-up/validation) |
| `firmware/m2_radar`  | the full radar with LCD dashboard ← **current** |

Build and flash:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" firmware/m2_radar
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" -p /dev/cu.usbmodemXXX firmware/m2_radar
```

Requires the `esp32:esp32` core and the M5Unified library. Native-USB ESP32-S3
serial quirks (shared with the sibling soil monitor): **opening the serial port
resets the device**; after a flash the chip parks in download mode — **single-
click the side button to boot**; if it sits dark with USB product "ESP32_S3"
it's in download mode (single-click to boot); if a flash fails to connect,
long-press the side button to force download mode, then retry.

## Display

240×135 landscape. What each element means and its range:

| Element | Meaning | Values |
|---------|---------|--------|
| `WiFi Radar` | header | fixed |
| `ch NN` (top right) | channel currently being sampled | 1–13, changes ~4×/sec |
| big number + `near` | nearby devices (non-AP, RSSI ≥ −75 dBm) | 0–999; dark grey 0 · green 1–3 · yellow 4–9 · red ≥ 10 |
| `NNN devs` | total non-AP devices seen in the last 60 s | 0–128 (device table cap) |
| `NNN APs` | access points seen in the last 60 s | 0–128 |
| chart | nearby-count history, one sample per 15 s (~last hour), auto-scaled | cyan line in a grey box |
| `fps NNN` | WiFi frames processed in the last second | 0–~200; drops to 0 only if no traffic at all |
| `bat NN%` | battery level from the PMIC | 0–100%, `--` if unavailable |
| `up H:MM` | time since power-on | hours unbounded, resets each boot |

Counts build up over the first ~60 s after boot as the window fills, then
track the environment. The chart's first point appears after 15 s.

## Privacy

By design: **receive-only** (no packets are ever transmitted — no deauth, no
probing, no injection), **nothing is logged or persisted** (no flash writes;
the sibling soil monitor's `/soil.csv` on the same device is untouched), and
**no MAC address is ever displayed or stored beyond the 60-second RAM window**.
Only aggregate counts appear on screen.

## Battery

The radio receives continuously, so expect roughly **2–3 hours** on the 250 mAh
cell — this is a walk-around toy, not an all-day logger. Runs indefinitely on
USB power.

## To do

- On-device controls (designed, not yet implemented): **KEY1** click to toggle
  the screen off/on while scanning continues (the backlight dominates battery
  draw), and **KEY2** hold 3 s to clear the window and chart with a confirming
  chirp.

## Repo layout

```
firmware/   Arduino sketches (m1 sniffer, m2 radar)
```

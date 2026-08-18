# Soil Moisture Monitor (M5StickS3)

A plant monitor built on the M5StickS3 and a capacitive soil moisture sensor
(v1.2) — the second device in this repo, a port of the
[Wio Terminal monitor](../../wio-terminal/soil-moisture-monitor/) with the
same measurements. Shows live moisture %, drying rate and a "water in ~X days"
estimate on the LCD, chirps when the plant needs water, and logs one sample
per minute to internal flash so history survives reboots.

**Maintainer:** George Babanau · actively maintained (Aug 2026)

## Hardware

- M5StickS3 (ESP32-S3-PICO-1-N8R8 — 3.3 V logic, GPIO **not** 5 V tolerant;
  8 MB flash, 1.14" 135×240 LCD, speaker, 250 mAh battery, **no SD slot** —
  logging uses the internal flash instead)
- Capacitive Soil Moisture Sensor v1.2, **unit #2** of the pack, powered from 3.3 V

Wiring on the top 16-pin HAT header (pin names are on the device sticker):

| Sensor | HAT pin  | Function |
|--------|----------|----------|
| VCC    | `3V3_L2` | 3.3 V    |
| GND    | `GND`    | GND      |
| AOUT   | `G7`     | ADC1_CH6 |

⚠️ Use `3V3_L2`, **not** `5V_IN`/`EXT_5V` — 5 V supply pushes AOUT near the
pin limit and breaks calibration comparability. G7 was chosen because it's an
ADC1 pin (keeps working when WiFi is on) with no strapping or alternate
function; avoid G0/G2/G3 (boot/display-rail/JTAG straps) and G43/G44 (debug
UART).

## Firmware

| Sketch | Adds |
|--------|------|
| `firmware/m1_bringup` | raw ADC + mV over serial (wiring/sensor validation) |
| `firmware/m2_monitor` | calibrated %, trends, ETA, flash logging, controls ← **current** |

Controls: **click KEY1** (front face button) to toggle the screen (logging
continues with it off); **hold KEY2 for 3 s** (small button next to the side
reset button) after moving the probe — it chirps, clears the trend history,
and writes a `#RESET` marker to the log. The **side button** is the system
reset: single-click reboots, double-click powers off, long-press enters
download mode.

Build and flash:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" firmware/m2_monitor
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" -p /dev/cu.usbmodemXXX firmware/m2_monitor
```

Requires the `esp32:esp32` core and the M5Unified library. Serial quirks of
the native-USB ESP32-S3: **opening the serial port resets the device**, the
flasher usually leaves it in download mode (**single-click the side button
after flashing** to boot), and if it ever sits dark with USB product
"ESP32_S3" it's stuck in download mode — single-click to boot. Calibration
anchors live at the top of the sketch — see [CALIBRATION.md](CALIBRATION.md).

## Display

What each element on the dashboard means and the values it can show:

| Element | Meaning | Possible values |
|---------|---------|-----------------|
| `Soil Moisture #2` | header; `#2` = sensor ID | fixed |
| `FS` / `FS!` (top right) | internal-flash log status | green `FS` = logging OK; red `FS!` = mount or write failed (remount retried every minute) |
| `76%` (large) | calibrated moisture, live reading (1 s refresh) | `0%`–`100%`, clamped; red < 30%, yellow 30–49%, green ≥ 50% |
| rate (right column) | drying rate — least-squares slope over the last ≤ 6 h of 1-min means | `N/30m` (`N` = 0–29) for the first 30 min after boot/watering/reset, then signed `±D.DD %/h` (positive = getting wetter) |
| ETA (below rate) | time until the 30% watering threshold, linear extrapolation | `ETA --` (still collecting) · `not drying` (rate ≥ −0.005 %/h) · `~Dd HHh` (days 0–99, hours 00–23) · `>99 days` · red `WATER NOW!` + chirp at ≤ 30% |
| chart | last 48 h of 1-min moisture means (cyan), restored from flash on boot | y-axis fixed 0–100%; red dotted line = 30% threshold |
| `raw` | latest 12-bit ADC median (lower = wetter) | 0–4095 theoretical; ~3573 (dry air) to ~1392 (water) for sensor #2 — pinned near 0/4095 means a wiring fault |
| `bat NN%` | battery level from the PMIC | 0–100%, `--` if unavailable |
| `up H:MM` | time since power-on | hours unbounded, resets each boot (unlike the CSV `minute` counter, which persists) |

The rate/ETA/chart refresh once per logging minute; the trend fit restarts
(→ `N/30m`) after a reboot, a watering event (> +5 points in 5 min), or a
KEY2 trend reset. Note `WATER NOW!` only replaces the ETA text once a rate
exists — during the 30-min warm-up the chirp is the low-moisture alert.

## Data

The device appends to `/soil.csv` on internal flash (LittleFS):

```
boot,minute,sensor,raw,pct
```

One row per minute, plus `#RESET,<minute>` marker rows (restore-on-boot
ignores data before the last marker). `minute` keeps counting across reboots;
time spent powered off is not counted. On boot the last 48 h are restored to
the sparkline; the drying-rate fit restarts. At ~1.5 MB (≈ 7 weeks) the log
rotates to `/soil.old.csv`, keeping ~3.5 months total.

To extract the data, connect a serial terminal (115200) and type `DUMP` —
it prints `/soil.old.csv` + `/soil.csv` and ends with `#DUMP_END`. The output
pipes straight into the Wio project's report tool:

```sh
python3 ../../wio-terminal/soil-moisture-monitor/tools/export_report.py soil.csv -o report.md --start 2026-08-18T12:00
```

## To do

Shared with the Wio project — see its
[to-dos](../../wio-terminal/soil-moisture-monitor/README.md#to-do)
(temperature/humidity in the ETA formula, Home Assistant via MQTT — the
StickS3's WiFi makes it the natural first target for the latter).

## Repo layout

```
firmware/       Arduino sketches (m1 bring-up, m2 monitor)
CALIBRATION.md  per-sensor calibration log + ESP32-S3 serial quirks
```

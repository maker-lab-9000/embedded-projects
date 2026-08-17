# M5StickS3 Soil Moisture Monitor — Design

**Date:** 2026-08-17
**Status:** approved design, pending implementation plan

## Goal

Port the Wio Terminal soil moisture monitor (`wio-terminal/soil-moisture-monitor`,
currently `firmware/m5_power`) to the M5StickS3, with feature parity: calibrated
moisture %, drying rate, watering ETA, 48 h trend chart, low-moisture alert,
per-minute logging that survives reboots, screen toggle, and on-device trend
reset. Second device, second sensor from the same 5-pack.

## Hardware

### Device: M5StickS3 (not the M5StickC PLUS2 originally assumed)

- ESP32-S3-PICO-1-N8R8: dual-core LX7 @ 240 MHz, WiFi, 8 MB flash, 8 MB octal PSRAM
- 1.14" ST7789P3 IPS, 135×240 (pins: MOSI G39, SCK G40, RS G45, CS G41, RST G21, BL G38)
- Buttons: KEY1 = G11 (front), KEY2 = G12; side button is system reset/power (untouched)
- Audio: ES8311 codec (I2C 0x18) + AW8737 amp + 1 W speaker over I2S
  (G18 MCLK, G14 DOUT, G17 BCLK, G15 LRCK) — replaces the Wio's passive buzzer
- Battery: 250 mAh; level/charge state via M5PM1 PMIC (I2C 0x6e), not an ADC pin
- Internal I2C bus (PMIC, IMU, codec): SCL G48, SDA G47
- No SD slot — persistence moves to internal flash (LittleFS)

### Sensor

Capacitive Soil Moisture Sensor v1.2, **unit #2** of the 5-pack. Calibration is
per-unit; anchors are measured during the calibration step and recorded in the
new project's CALIBRATION.md (same 2-point air/water procedure as the Wio).
`SENSOR_ID = 2` in firmware and CSV.

### Wiring — top HAT header (16-pin)

| Sensor | HAT pin  | Rationale |
|--------|----------|-----------|
| VCC    | `3V3_L2` | 3.3 V supply, same as Wio build so readings are comparable. PMIC-gated LDO rail — bring-up verifies it outputs 3.3 V and, if not, firmware enables it via M5PM1. |
| GND    | `GND`    | |
| AOUT   | `G7`     | ADC1_CH6. ADC1 (GPIO 1–10) keeps working when WiFi is on (future MQTT). G7 has no strapping or alternate function. |

Pins deliberately avoided: G2 (display-rail enable), G3 (JTAG strapping),
G43/G44 (debug UART), G0 (boot), G9/G10 (Grove, reserved for a future Grove
sensor), Grove 5 V (input mode by default; 5 V supply also pushes AOUT near the
pin limit and breaks calibration comparability).

### ESP32-S3 ADC notes

- 12-bit like the SAMD51, but nonlinear; set ~12 dB attenuation on G7 for a
  usable range up to ≈3.1 V.
- Keep the median-of-15-samples-2-ms-apart read. Prefer
  `analogReadMilliVolts()` in bring-up (uses the factory eFuse calibration).
- Known risk: dry-air AOUT measured ≈2.97 V on the Wio — inside range but near
  the top. Bring-up explicitly checks the air anchor isn't clipping; if it is,
  fall back to a different attenuation or note the compressed top in
  CALIBRATION.md (only the air anchor region is affected).

## Repo layout

```
m5sticks3/soil-moisture-monitor/
  README.md            hardware, wiring, display reference, build, data, to-dos
  CALIBRATION.md       sensor #2 anchors + procedure
  firmware/m1_bringup/m1_bringup.ino
  firmware/m2_monitor/m2_monitor.ino
```

Root `README.md` project table gets a row. Shared to-dos (temp/humidity in ETA,
Home Assistant/MQTT) are referenced, not duplicated.

## Toolchain

- `esp32:esp32` Arduino core; FQBN `esp32:esp32:esp32s3` with board options:
  8 MB flash, OPI PSRAM, USB CDC on boot (native USB — serial appears as
  `cu.usbmodem*`), partition scheme with a LittleFS/SPIFFS data partition
  (~1.5 MB or larger). Exact option strings pinned during setup and recorded in
  the README build commands.
- Libraries: M5Unified + M5GFX (display, buttons, speaker), M5PM1 (battery,
  power rails). LittleFS ships with the core.

## Firmware

### m1_bringup

Serial-only validation: every second print raw ADC (median of 15) and
millivolts for G7. Validates wiring, the 3V3_L2 rail, air-vs-water delta
(should be several hundred counts), and no clipping at the dry-air end.

### m2_monitor — full port of m5_power logic

Algorithms and constants carried over unchanged:

- 1 s live reading (median of 15, 2 ms apart); 1-minute mean committed to history
- Ring buffer 2880 × 1-min samples = 48 h
- Watering detection: mean rises > +5 points vs 5 minutes earlier → trend restart
- Rate: least-squares slope over trailing ≤ 360 samples (6 h), needs ≥ 30 samples
- ETA: `hours = (pct − 30) / |rate|`; states `--` / `not drying` (rate ≥ −0.005)
  / `Water in ~Dd HHh` / `>99 days` / `WATER NOW!` at ≤ 30 %
- Alert chirp at ≤ 30 %, re-arms above 35 % — played through the speaker
  (M5Unified `Speaker.tone`), same two-tone chirp

Differences, forced by hardware:

- **Persistence: LittleFS** on internal flash at `/soil.csv`. Same CSV format
  `boot,minute,sensor,raw,pct` + `#RESET,<minute>` markers; same tail-restore
  on boot (last ~90 KB, ignore rows before the last `#RESET`); trend fit
  restarts after boot (off-time unknown).
- **Log rotation** (new — flash is not a swappable card): when `/soil.csv`
  exceeds ~1.5 MB, rename to `/soil.old.csv` (replacing any previous one) and
  start fresh. ≈ 7 weeks per file, ~3.5 months retained across both.
- **`DUMP` serial command** (new): typing `DUMP` over USB serial prints
  `/soil.old.csv` + `/soil.csv` contents; output pipes into the existing
  `tools/export_report.py` unchanged. `WIPE` intentionally not implemented —
  rotation bounds growth.
- **Display 240×135 landscape**, condensed layout:

  ```
  Soil Moisture #2   FS        header (small) + filesystem badge
  76%    +2.96 %/h             moisture (large, left) · rate (right)
         not drying            ETA (right column)
  [ sparkline + threshold ]    ~44 px tall, full width
  raw 2076  bat 87%  up 0:52   diagnostics
  ```

  Same color semantics as the Wio (moisture bands red <30 / yellow <50 /
  green ≥50; ETA states; green `FS` / red `FS!` badge for LittleFS health).
  New `bat NN%` field from the M5PM1.
- **Controls:** KEY1 click = screen off/on (backlight + display sleep;
  sampling/logging continue). KEY2 held 3 s = trend reset: clear history,
  append `#RESET` marker, chirp.

## Validation plan

1. Wiring seated per the table (user re-seats the three jumpers; photo check).
2. m1_bringup: 3V3_L2 measures ~3.3 V under load; stable raw values; air vs
   water delta of several hundred counts; no clipping in dry air.
3. Calibration: record `RAW_AIR` / `RAW_WATER` for sensor #2 in CALIBRATION.md.
4. m2_monitor: display renders all states; one CSV row per minute confirmed via
   `DUMP`; reboot restores the sparkline and reads `collecting N/30m`; KEY1
   toggles screen while logging continues; KEY2 3 s reset writes `#RESET` and
   chirps; threshold chirp fires (probe lifted into air).

## Risks / open questions

- 3V3_L2 rail may be disabled by default → enable via M5PM1 in `setup()`;
  verified at bring-up.
- Dry-air anchor near ADC ceiling → checked at bring-up (see ADC notes).
- No dedicated StickS3 entry in the Arduino board index yet → generic ESP32-S3
  FQBN with explicit options; M5Unified/M5GFX StickS3 support assumed per
  M5Stack docs, verified at first compile.
- `usbmodem14101` assumed to be the StickS3 (native USB CDC); confirmed at
  first flash.

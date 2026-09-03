# Orbital Density (Wio Terminal)

A Wio Terminal that visualizes what's *above* it. **Milestone 1 — GPS sky view:**
a live polar "radar" of every satellite the Air530 GNSS module can hear
(GPS + BeiDou + GLONASS), colored by constellation, plus a numeric
detail page, a 24-hour satellite-count chart, reception-anomaly alerts, the
live positions of the **Sun, Moon, and five naked-eye planets** on the sky
(computed from a low-precision ephemeris using GPS position + UTC), and microSD
logging with a real UTC clock from the satellites. **Plus a first extra
sensor:** a Grove Dust Sensor feeding a relative "dust activity" page.
**Second extra sensor:** a BME280 (temperature, humidity, pressure) with a
24-hour chart and a **pressure-trend weather forecaster** (storm / rain /
change / fair / stable). A **24H Observation** page shows per-constellation
satellite statistics over the last 24 hours.

<img src="docs/page-sky.jpg" width="480" alt="Sky page: polar radar plot with GPS (green) and BeiDou (magenta) satellites, elevation rings, compass N/E/S/W, Mars indicator, battery 91%, SD badge">

All eight display pages (Dust is compiled out while the sky sensors are integrated):

<img src="docs/page-sky.jpg" width="240" alt="Sky page"> <img src="docs/page-detail.jpg" width="240" alt="Detail page">
<img src="docs/page-chart.jpg" width="240" alt="Chart page"> <img src="docs/page-dust.jpg" width="240" alt="Dust page (DUST_ENABLED 1)">
<img src="docs/page-env.jpg" width="240" alt="Env page"> <img src="docs/page-skysens.jpg" width="240" alt="SkySens page">
<img src="docs/page-mag.jpg" width="240" alt="Mag page"> <img src="docs/page-sensors.jpg" width="240" alt="Sensors page">

The **sky-observatory milestone** (Sept 2026) adds a TSL2591 optical sky sensor and an
MMC5603 magnetometer over a Grove I2C hub, a unified 1 Hz observation log (`/obs.csv`), and
three pages. The MLX90614 thermal-sky channel is wired into the design but deferred until its
GY-906 board is soldered (`MLX_ENABLED`). Design spec: `docs/superpowers/specs/`, task plan:
`docs/superpowers/plans/`.

**Maintainer:** George Babanau · actively maintained (Aug 2026)

## Hardware

- Seeed Wio Terminal (SAMD51).
- Air530Z GNSS module (AT6558R chipset, NMEA 0183 over UART @ 115200 baud,
  configured via `$PCAS` commands). The module supports dual-constellation
  mode only: GPS+BeiDou or GPS+GLONASS. The firmware alternates between
  the two modes every 45 seconds so all three constellations appear on
  the display (satellites persist for 120 s across mode switches).
  Supported constellations: **GPS** (USA), **GLONASS** (Russia),
  **BeiDou** (China), **QZSS** (Japan, receive-only augmentation —
  reported as GPS PRNs 193–199, reclassified by the parser). **Galileo
  is not supported** by this module (no mode, system ID, or command in
  the AT6558R documentation). QZSS is unlikely to be visible outside
  the Asia-Oceania region.
- Grove Dust Sensor (Shinyei PPD42NS), digital pulse output.
- Seengreat BME280 Environmental Sensor (temperature, humidity, pressure via I2C).
- Wio Terminal Chassis – Battery (650 mAh) with BQ27441 fuel gauge (I2C 0x55); adds 1 Grove UART,
  1 Grove I2C and 4 Grove analog/digital sockets fed from the 40-pin header.
- Adafruit TSL2591 (STEMMA QT) — optical sky brightness, visible + near-IR, I2C 0x29.
- Adafruit MMC5603 (STEMMA QT) — magnetometer on a 15–30 cm non-magnetic arm, I2C 0x30.
- MLX90614ESF-BCC on a GY-906 module — long-wave IR sky brightness temperature, SMBus 0x5A
  (3 V, 35° FOV, gradient compensated). **Deferred** until its header is soldered (`MLX_ENABLED 0`).
- Grove I2C Hub (4 sockets, passive) + 2× Adafruit 4528 Grove-to-STEMMA QT cable (or 1× plus a
  QT-to-QT cable) + 1 plain Grove cable for the GPS; Grove-to-female-jumper cable for the GY-906 later.

### GNSS wiring — chassis Grove UART socket (= `Serial1`)

The Air530Z plugs into the battery-chassis socket labelled **`RX TX`** (bottom edge, beside the
USB-C; labels are on the back of the chassis) with a plain Grove cable. Per Seeed's schematic that
socket is header pins 10/8 (RXD/TXD), i.e. `Serial1` — the same UART the firmware has always used,
so nothing changes in code. ⚠️ The four `IO*` sockets look identical and are plain GPIO: a GPS
plugged into one of them is silent (`firmware/m1_pinsweep` finds where its TX line really lands).

Fallback without the chassis: jumper-wire the module to the header:

| GPS wire | Wio header pin |
|----------|----------------|
| TX       | **pin 10** (BCM15 / RXD / `Serial1` RX) |
| RX       | **pin 8** (BCM14 / TXD / `Serial1` TX) — used for PCAS baud/mode commands |
| VCC      | pin 1 (3V3) |
| GND      | pin 6 (GND) |

⚠️ **The D0/D1 (A0/A1) Grove port does _not_ work for this.** Those pins are
PB08/PB09 (SERCOM4/PAD0-1, function D); the signal arrives but the SERCOM
would not latch it in testing. Only the header UART (`Serial1`, on PB26/PB27 =
BCM14/BCM15) reliably reads the module. Use a Grove-to-female-jumper cable from
the module to the header pins above.

Three things stack up against the right Grove port: the core maps D0/D1 as analog pins, so a
`Uart` needs an explicit `pinPeripheral(..., PIO_SERCOM_ALT)`; their only SERCOM is SERCOM4,
whose TX must be on pad 0 = D0 (the opposite of the Grove UART pin order, so the cable would
need TX/RX swapped); and the core's `Wire.cpp` already defines the SERCOM4 interrupt handlers
for `Wire1`, so any sketch that uses `Wire` and defines them too fails to link.

### Dust sensor wiring — the D0/D1 Grove port (plain GPIO works fine here)

> Currently compiled out (`#define DUST_ENABLED 0` in `m2_skyview.ino`) while the sky sensors are
> integrated. When it returns it goes in the chassis `IO0 IO1` socket, whose pin 1 is `D0`.

The dust sensor's signal (yellow) plugs into the Wio's **D0/D1 (A0/A1) Grove
port**, signal on **D0**. Unlike the GPS, this works fine there: the sensor
only needs a plain digital input (pulse-width timing), not a UART, and that's
a normal GPIO function on those pins.

⚠️ **Grove VCC is 3.3V; this sensor is rated 5V.** Readings are therefore a
relative "dust activity" indicator, not a calibrated concentration — confirmed
working via a real dust stimulus (vacuuming near the sensor produced a clear,
repeatable response). **Do not power it from the Wio's 5V pin without also
adding a resistor divider on the signal line** — no SAMD51 GPIO pin is
5V-tolerant (confirmed against the datasheet), so a 5V-driven output would risk
damaging whichever pin it's wired to.

### BME280 wiring — Grove I2C Hub socket

The Seengreat BME280 plugs into one socket of the **Grove I2C Hub** on the left Grove port via
its Grove cable (same `Wire` bus as before). VCC = 3.3V, GND, SCL, SDA.

The sensor's ADDR jumper selects address **0x76 or 0x77**; `m1_bme280` auto-detects both,
`m2_skyview` is hard-coded to 0x77. No conflict with the BQ27441 fuel gauge (0x55) — they
share the Wire bus.

⚠️ **Don't use `pulseIn()` for this sensor on this core.** It does not return
`0` on a clean timeout — confirmed at bring-up: it returned a bogus
near-`ULONG_MAX` value on essentially every timeout, at a rate matching its own
1-second internal timeout exactly. `firmware/m2_skyview` instead polls the pin
with `digitalRead()` + `micros()` every loop iteration, which sidesteps the bug
entirely.

### Sky-observatory sensors — Grove I2C Hub on the LEFT Grove port

The hub is passive (four sockets in parallel): one is the uplink, the other three take the
BME280, TSL2591 and MMC5603, one each. The chassis I2C socket is spare for the GY-906. The
right Grove port (D0/D1) is digital/analog only: it cannot host I2C, nor a UART in this
firmware (see the GNSS section).

```
                 Wio Terminal + 650 mAh battery chassis
                 ┌────────────────────────────────────────────────────┐
 LEFT Grove port │ SDA/SCL (Wire)        chassis Grove UART (Serial1) │── Grove cable ── Air530Z GNSS
                 │                       chassis Grove I2C  (Wire)    │   (spare; GY-906 later)
                 │                       chassis A/D ×4               │   (dust sensor, later)
                 └───┬────────────────────────────────────────────────┘
                     │ Grove cable
                ┌────┴──── Grove I2C Hub (4 sockets, all parallel) ─────┐
                │ [uplink]       [2]            [3]            [4]      │
                └─────────────────┬──────────────┬──────────────┬───────┘
                            Grove cable    #4528 Grove→QT   #4528 Grove→QT
                                  │              │              │
                              BME280         TSL2591        MMC5603
                              (ventilated)   (zenith)       (on the arm)
```

| Address | Device |
|---:|---|
| 0x29 | TSL2591 |
| 0x30 | MMC5603 |
| 0x55 | BQ27441 (battery chassis) |
| 0x5A | MLX90614 (deferred, `MLX_ENABLED 0`) |
| 0x77 | BME280 (hard-coded in `m2_skyview`; the ADDR jumper must stay at 0x77) |

Chassis socket labels (back of the chassis board): `RX TX` = header 10/8 (`Serial1`), `SCL SDA` =
header 5/3 (`Wire`), `IO0 IO1` = D0/D1, `IO2 IO3` = D2/D3, `IO4 IO5` = D4/D5, `IO6 IO8` = D6/D8.

⚠️ **The bus runs at 100 kHz and must stay there** — the MLX90614 is an SMBus device.
Each board carries its own pull-ups; if the boot scan (`I2C scan:` on USB serial, or the
Sensors page) misses a device intermittently, remove one board's pull-up resistors.

The MMC5603 needs the chip's **automatic set/reset** enabled (CTRL0 bit 5); the Adafruit library
does not do it and the raw bridge offset is up to ±100 µT per axis — bring-up read a steady
173 µT "field" until `mmc5603.h` turned it on.

## Firmware

| Sketch | Purpose |
|--------|---------|
| `firmware/m1_gps`     | serial-only bring-up: NMEA read + TinyGPS++ summary |
| `firmware/m1_dust`    | serial-only bring-up: dust sensor pulse polling |
| `firmware/m1_bme280`  | serial-only bring-up: I2C scan + BME280 temp/humidity/pressure |
| `firmware/m1_i2c_scan` | hub bus scan + Serial1 NMEA byte count with auto-baud (confirms the chassis UART socket) |
| `firmware/m1_pinsweep` | counts edges on every 40-pin-header GPIO: which header pin is a chassis-socket signal on? |
| `firmware/m1_tsl2591` | serial-only bring-up: non-blocking TSL2591 register driver (`tsl2591.h`), auto-range |
| `firmware/m1_mmc5603` | serial-only bring-up: MMC5603 continuous mode + auto set/reset, read-time check |
| `firmware/m1_mlx90614` | serial-only bring-up for the GY-906 — **deferred** until soldered |
| `firmware/m2_skyview` | full UI: sky/detail/chart/env/obs/skysens/mag/sensors pages, `/gps.csv` + `/obs.csv` logging ← **current**; the sketch plus header modules (`loopstats.h`, `i2c_bus.h`, `tsl2591.h`, `ring.h`, `mmc5603.h`, `observation.h`, `pages_sensors.h`) |

Build and flash (close any serial monitor first — it locks the port):

```sh
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal firmware/m2_skyview
arduino-cli upload  --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodemXXX firmware/m2_skyview
```

Requires the Seeed SAMD board package and libraries **TinyGPSPlus**,
**Seeed Arduino FS** (SD), **SparkFun BQ27441** (battery), **Adafruit MMC56x3** (magnetometer;
pulls in Adafruit BusIO and Unified Sensor), and TFT_eSPI (bundled with the Seeed core).
**Adafruit MLX90614 Library** only once `MLX_ENABLED` is 1. The TSL2591 uses the local
register-level `tsl2591.h` because the Adafruit library sleeps up to 720 ms per read. The GPS module needs a clear-ish sky / windowsill; cold
start takes 1–2 minutes to a fix.

### Loop timing baseline (commit 3438f12, hub + chassis attached, no new drivers)

Measured 2026-09-03 over 5 minutes with the screen on and a 3D fix, via the 1 Hz status line:

| metric | value |
|---|---|
| loopMax, screen on (median / max) | 138 ms / 151 ms |
| loop iterations per second (median) | ~234 000 |
| NMEA sentences passed | 7.4 per second |
| NMEA checksum failures | 111 per 5 min (≈0.37 per second, ≈5 % of sentences) |

The 1 Hz block (77 KB sprite blit + double-precision ephemeris) is the ~138 ms stall, and it
already costs about 5 % of NMEA sentences when it overlaps the module's once-a-second burst
(the UART RX buffer is 256 bytes, ~22 ms at 115200 baud). Every sensor task is accepted only
if the failed-checksum rate stays within this baseline. Fallback if it does not: build with
`--build-property "compiler.cpp.extra_flags=-DSERIAL_BUFFER_SIZE=1024"`.

## Display & controls

Eight pages: Sky → Detail → Chart → Env → Obs → SkySens → Mag → Sensors → back to Sky
(Dust reappears between Chart and Env when `DUST_ENABLED` is 1). **Top-left button (KEY_C)**
or **5-way right** = next page, **5-way left** = previous page (both wrap). The **5-way
center-press toggles the screen** on/off (GPS parsing, sensor polling, and SD logging keep
running while it's off — the backlight is the main power draw).

Header (all pages): `V <in-view>  U <used>  <fix>`, battery % (if the chassis
fuel gauge is present), and a green `SD` / red `SD!` badge.

- **Sky page** — polar plot: center = zenith, rim = horizon (ring at 30°/60°
  elevation), compass N/E/S/W. Each satellite is a dot **colored by
  constellation** (GPS green, GLONASS cyan, BeiDou magenta, QZSS yellow),
  sized by signal strength. Satellites that are heard but not yet positioned
  (before a fix, or without ephemeris) show as an "N unlocated" count.
  The **Sun, Moon, and five naked-eye planets** (Mercury, Venus, Mars,
  Jupiter, Saturn) are plotted at their true alt/azimuth when above the
  horizon, each with a distinct color and label. Positions come from a
  low-precision Schlyter ephemeris computed from GPS position + UTC. The plot
  assumes the device is pointed north — there's no on-board compass.
- **Detail page** — numeric: in view (and positioned), per-constellation
  counts, satellites used, fix type, HDOP, strongest SNR, lat/lon, altitude,
  UTC time.
- **Chart page** — satellites over the last **24 h**, split by constellation:
  GPS (green), GLONASS (cyan), BeiDou (magenta), unlocated (orange). UTC
  **hour markers** (00–23) along the bottom, value gridlines (0 / mid / max)
  on the left. Shows the current total (`now N`) and a white **peak marker**
  labelled with the peak value and the UTC time it occurred
  (`peak N @HH:MM`). Samples once every 5 minutes.
- **Dust page** — `Ratio: X.XX%   N pcs` (LPO ratio and a Shinyei-curve
  concentration estimate, both labelled "relative activity - uncalibrated"),
  plus a 24-hour rolling chart sampled once every 5 minutes. Yellow line,
  `0`/max gridlines, UTC hour markers along the bottom.
- **Env page** — current temperature (°C), humidity (%RH), and pressure (hPa),
  plus a **weather forecast** based on the 3-hour pressure trend: `STORM
  LIKELY` (red, >3 hPa drop + high humidity), `RAIN POSSIBLE` (yellow),
  `CHANGE` (orange, moderate drop), `FAIR` (green, rising pressure), or
  `STABLE` (grey). Shows "forecast in ~3h" until enough data accumulates.
  Below the values: a 24-hour triple-line chart — temperature (cyan, left
  axis), humidity (green), pressure (yellow, right axis auto-scaled) — with
  UTC hour markers. Sampled every 5 minutes.
- **Obs page** — 24-hour observation statistics table: per-constellation
  (GPS, GLONASS, BeiDou, QZSS) rows showing current in-view and used
  counts, plus 24-hour peak, average, and minimum values for each. Updated
  every 5 minutes from the rolling history buffer.
- **SkySens page** — TSL2591 optical sky: visible lux (or raw counts when saturated or
  dark), near-IR fraction, raw full/IR counts with the gain and integration time that produced
  them (auto-ranged between 1× / 100 ms and 9876× / 600 ms). Thermal rows read "Sky IR not
  installed" and Condition `--` until the MLX90614 is enabled; then Sky IR °C, Δ sky and a
  provisional CLEAR?/MIXED?/CLOUDY? label with a 24 h Δ chart.
- **Mag page** — MMC5603: |B| in µT, heading (level assumed, no tilt compensation), X/Y/Z
  one-second means, sample/error counters and the hard-iron offsets in use, plus a 24 h |B|
  chart sampled every 5 minutes.
- **Sensors page** — every I2C device with OK/missing and seconds since its last sample, the
  boot-time bus scan, loop max time and iterations/s, NMEA pass/fail counters, and `/obs.csv`
  rows written / write errors / rows buffered. Missing sensors are re-probed every 30 s, so
  hot-plugging works.

### Reception anomaly detection

Once a fix has been established, a red banner flags reception-health problems:
`FIX LOST`, `HI HDOP` (poor geometry), `LOW SAT` (few satellites used), or
`LOW SNR` (all signals weak) — the signatures of obstruction, interference, or
jamming. It's heuristic reception monitoring, not certified anti-spoofing. The
active state is also written to the `anom` column of every log row.

**In view vs used vs unlocated:** *in view* = satellites the receiver hears
(GSV). *used* = the subset locked into the position fix (GGA). *unlocated* =
in-view satellites whose sky position isn't known yet.

## Data

Logs one row per minute to `/gps.csv` on the microSD:

```
utc,uptime_s,in_view,positioned,used,fix,hdop,gps,glonass,beidou,qzss,anom,dust_ratio,dust_conc,temp_c,humidity,pressure_hpa,weather
```

`anom` is the reception-anomaly state at that minute (`OK` or a code like
`LOW SAT`). `dust_ratio`/`dust_conc` are the dust sensor's latest 30-second-window
values (see the Dust page above) — relative/uncalibrated, not µg/m³; empty while
`DUST_ENABLED` is 0.
`temp_c`/`humidity`/`pressure_hpa` are from the BME280; `weather` is the
current forecast state (`STORM LIKELY`, `RAIN POSSIBLE`, `CHANGE`, `FAIR`,
`STABLE`, or `WAIT` during the initial 3-hour warm-up).

`utc` is a real ISO timestamp from the GPS clock (blank until the module reports a
plausible date — TinyGPS++ flags the date valid even when the RMC date field is empty, which
showed up as `2000-00-00`; the satellites are the clock, so no RTC/battery is needed);
`uptime_s` is always present as a fallback.

### Observation log — `/obs.csv` (1 Hz)

One row per second, header generated from `OBS_COLS` in `firmware/m2_skyview/observation.h`:

```
utc,uptime_s,lat,lon,alt_m,fix,hdop,used,in_view,tsl_full,tsl_ir,tsl_vis,tsl_gain,tsl_integ_ms,tsl_lux,tsl_sat,mlx_ambient_c,mlx_object_c,mlx_delta_c,mag_x_ut,mag_y_ut,mag_z_ut,mag_total_ut,heading_deg,temp_c,humidity,pressure_hpa,sensors_ok,loop_max_ms
```

Absent sensors leave their fields empty (the `mlx_*` columns are empty until the GY-906 is
enabled). `sensors_ok` bits: 1 TSL, 2 MLX, 4 MAG, 8 BME. Magnetic values are one-second means
of the 10 Hz stream. About 180 bytes per row, roughly 15 MB per day. Rows are batched in RAM
and written with one open/append/close every 10 s, so a power cut loses at most 10 s.
`OBS_EVERY_N` in `m2_skyview.ino` slows the cadence; `/gps.csv` is unchanged.

### Magnetometer calibration

`tools/mag_calib.py offsets obs.csv --start S --end S` fits hard-iron offsets from a slow,
level 360° rotation (min/max midpoint per axis); `... stability ...` reports |B| mean and
standard deviation over a window, for choosing the arm length. Results go into `MAG_OFF_X/Y/Z`,
`MAG_MOUNT_OFFSET_DEG` and `MAG_DECLINATION_DEG` in `firmware/m2_skyview/mmc5603.h`. Not yet
calibrated: a quick hand rotation on 2026-09-03 suggested roughly x +23 µT, y +1 µT.

## To do

Future sensors (the broader "orbital density" vision), each an added
sensor + page/columns:

- **Magnetometer / compass** — done (MMC5603 on the hub, Mag page, `mag_*` columns).
  Still to do: field calibration (`tools/mag_calib.py`, arm length, hard-iron offsets),
  then auto-orient the sky plot to true North instead of assuming the device points north;
  tilt compensation from the built-in LIS3DHTR on `Wire1` later.
- **Ambient-light sensor** — done (TSL2591, SkySens page, `tsl_*` columns). Still to do:
  night-sky baselines (clear / overcast / moonlit) before any condition classifier.
- **Thermal sky (MLX90614 / GY-906)** — designed and gated behind `MLX_ENABLED`; solder the
  header, run the deferred bring-up, flip the flag.
- Dust sensor: if better accuracy is ever wanted, power from 5V with a
  resistor divider on the signal line (see the wiring warning above) — not
  pursued for now since 3.3V already gives a usable relative signal.

## Repo layout

```
firmware/          Arduino sketches (m1 bring-ups, m2 full sky view + header modules)
tools/             mag_calib.py — magnetometer offsets / stability from obs.csv
docs/              page screenshots; docs/superpowers/{specs,plans}/ — sky-observatory design + task plan
PLAN.md            original milestone plan (historical)
wio-sky-observatory-integration.md   sensor concept document
```

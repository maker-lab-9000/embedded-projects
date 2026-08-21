# Orbital Density (Wio Terminal)

A Wio Terminal that visualizes what's *above* it. **Milestone 1 — GPS sky view:**
a live polar "radar" of every satellite the Air530 GNSS module can hear
(GPS + GLONASS + Galileo + BeiDou), colored by constellation, plus a numeric
detail page, a 24-hour satellite-count chart, and microSD logging with a real
UTC clock from the satellites.

Planned later milestones add a **dust/particulate sensor** and an
**ambient-light sensor** — the firmware and CSV are structured so those slot in
as extra sensors, pages, and columns.

**Maintainer:** George Babanau · actively maintained (Aug 2026)

## Hardware

- Seeed Wio Terminal (SAMD51).
- Air530 GNSS module (multi-constellation, NMEA 0183 over UART @ 9600).

### Wiring — use the 40-pin HEADER UART, not a Grove port

The Air530 must connect to the Wio's hardware UART on the **40-pin header**:

| GPS wire | Wio header pin |
|----------|----------------|
| TX       | **pin 10** (BCM15 / RXD / `Serial1` RX) |
| RX       | pin 8 (BCM14 / TXD) — optional, GPS only transmits |
| VCC      | pin 1 (3V3) |
| GND      | pin 6 (GND) |

⚠️ **The D0/D1 (A0/A1) Grove port does _not_ work for this.** Those pins are
PB08/PB09 (SERCOM4/PAD0-1, function D); the signal arrives but the SERCOM
would not latch it in testing. Only the header UART (`Serial1`, on PB26/PB27 =
BCM14/BCM15) reliably reads the module. Use a Grove-to-female-jumper cable from
the module to the header pins above.

## Firmware

| Sketch | Purpose |
|--------|---------|
| `firmware/m1_gps`     | serial-only bring-up: NMEA read + TinyGPS++ summary |
| `firmware/m2_skyview` | full UI: sky plot, detail page, 24 h chart, SD logging ← **current** |

Build and flash (close any serial monitor first — it locks the port):

```sh
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal firmware/m2_skyview
arduino-cli upload  --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodemXXX firmware/m2_skyview
```

Requires the Seeed SAMD board package and libraries **TinyGPSPlus**,
**Seeed Arduino FS** (SD), **SparkFun BQ27441** (battery), and TFT_eSPI (bundled
with the Seeed core). The module needs a clear-ish sky / windowsill; cold start
takes 1–2 minutes to a fix.

## Display & controls

Three pages, cycled with the **top-left button (KEY_C)**. The **5-way switch
center-press toggles the screen** on/off (GPS parsing and SD logging keep
running while it's off — the backlight is the main power draw).

Header (all pages): `V <in-view>  U <used>  <fix>`, battery % (if the chassis
fuel gauge is present), and a green `SD` / red `SD!` badge.

- **Sky page** — polar plot: center = zenith, rim = horizon (ring at 30°/60°
  elevation), compass N/E/S/W. Each satellite is a dot **colored by
  constellation** (GPS green, GLONASS cyan, Galileo orange, BeiDou magenta),
  sized by signal strength. Satellites that are heard but not yet positioned
  (before a fix, or without ephemeris) show as an "N unlocated" count.
- **Detail page** — numeric: in view (and positioned), per-constellation
  counts, satellites used, fix type, HDOP, strongest SNR, lat/lon, altitude,
  UTC time.
- **Chart page** — satellites over the last **24 h**: green = in view, orange =
  unlocated, with UTC **hour markers** (00–23) along the bottom. Samples once
  every 5 minutes.

**In view vs used vs unlocated:** *in view* = satellites the receiver hears
(GSV). *used* = the subset locked into the position fix (GGA). *unlocated* =
in-view satellites whose sky position isn't known yet.

## Data

Logs one row per minute to `/gps.csv` on the microSD:

```
utc,uptime_s,in_view,positioned,used,fix,hdop,gps,glonass,galileo,beidou
```

`utc` is a real ISO timestamp from the GPS clock (blank until the first fix —
the satellites are the clock, so no RTC/battery is needed); `uptime_s` is
always present as a fallback.

## To do

- Dust / particulate sensor + ambient-light sensor (the broader "orbital
  density" vision) as additional sensors, pages, and CSV columns.

## Repo layout

```
firmware/   Arduino sketches (m1 GPS bring-up, m2 sky view)
PLAN.md     design & implementation plan
```

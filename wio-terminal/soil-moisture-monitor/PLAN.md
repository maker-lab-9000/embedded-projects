# Soil Moisture Monitor — Wio Terminal + Capacitive Soil Moisture Sensor v1.2

Plan for connecting the APKLVSR "Capacitive Soil Moisture Sensor v1.2" (5-pack) to a
Seeed Wio Terminal, measuring soil moisture, and estimating the drying rate / time
until the plant needs watering.

> **Status (2026-08-16):** Milestones 1–3 complete, plus SD logging from
> Milestone 4 (`firmware/m4_sdlog` is on the device). Remaining M4 ideas:
> sensors #2–5, WiFi/MQTT → Home Assistant, temperature/humidity input to the
> ETA formula (needs an external temp/RH sensor). See README.md for current
> usage.

---

## 1. Hardware background

### The sensor (Capacitive Soil Moisture Sensor v1.2)
- A 555-timer oscillator drives the probe; the probe's capacitance rises with soil
  water content. An RC peak detector converts this to a DC voltage on **AOUT**.
- **Analog output, inverted scale: wet soil → LOW voltage, dry soil → HIGH voltage.**
- 3 pins: `GND`, `VCC`, `AOUT` (marked on the board). Rated 3.3–5.5 V, draws ~5 mA.
- Being capacitive (no exposed electrodes), it does not corrode/electrolyze like the
  cheap resistive fork sensors — fine to power continuously.
- **Clone caveat:** some v1.2 clones ship with an NE555 that only works reliably at
  5 V instead of the CMOS TL555 that works at 3.3 V. Validation step in Milestone 1
  catches this: if dry-air vs in-water readings barely differ at 3.3 V, that unit
  needs 5 V supply — in that case measure AOUT with a multimeter first (typically
  tops out ~3.0 V at 5 V supply) before connecting it to the Wio Terminal, or add a
  voltage divider.

### The Wio Terminal
- SAMD51 (Cortex-M4F), **3.3 V logic — GPIO pins are NOT 5 V tolerant.**
- 12-bit ADC (Arduino default is 10-bit; enable 12-bit with `analogReadResolution(12)`).
- Rear 40-pin Raspberry-Pi-compatible header, plus two Grove ports on the bottom:
  **left (facing screen) = I2C, right = analog/digital exposing A0/D0**.
- Analog pins `A0`–`A8` are defined in the Seeed Arduino core (A0 = PB08/BCM27,
  A1 = PB09/BCM22, A2 = PA04, …).
- Useful onboard extras for this project: 2.4" LCD, 3 user buttons (top), buzzer,
  microSD slot, RTC, WiFi (RTL8720), light sensor.

## 2. Wiring

### Option A — right Grove port (cleanest, 1 sensor)
Use a Grove-to-female-jumper conversion cable on the **right** Grove port:

| Grove wire            | Sensor pin |
|-----------------------|-----------|
| Yellow (signal 1 = A0)| AOUT      |
| White (signal 2)      | — unused  |
| Red (3.3 V)           | VCC       |
| Black (GND)           | GND       |

### Option B — 40-pin rear header (scales to multiple sensors)
The pack's cables are JST-PH 3-pin → DuPont-style female; the female ends push
directly onto the header pins.

| Sensor pin | Header physical pin | Function            |
|-----------|---------------------|----------------------|
| VCC       | **Pin 1**           | 3.3 V                |
| GND       | **Pin 6** (or 9, 14, 20, 25, …) | GND      |
| AOUT (sensor 1) | **Pin 13**    | A0 (BCM27, PB08)     |
| AOUT (sensor 2) | **Pin 15**    | A1 (BCM22, PB09)     |
| AOUT (sensors 3–5) | see pinout diagram | A2–A4         |

- **Verify wire colors with a continuity test** — the JST cable colors don't always
  match the GND/VCC/AOUT silkscreen order on the sensor.
- **Power sensors from 3.3 V (pin 1), not 5 V.** At 3.3 V the AOUT range is safely
  inside the ADC range; 5 V supply risks AOUT approaching/exceeding 3.3 V.
- Multiple sensors can share the 3.3 V and GND pins (5 sensors ≈ 25 mA, fine on USB
  power).
- Optional (battery use): power sensor VCC from a digital pin and only energize
  ~200 ms before each reading. One sensor (~5 mA) is within a SAMD51 pin's drive
  strength; for several sensors switch them via a P-MOSFET high-side instead.

### Physical installation
- Insert the probe only up to just below the electronics area (roughly the line
  above the pointed blade) — **the SMD components at the top must stay dry**.
- For longevity, seal the top edge / component area with heat-shrink or nail polish;
  moisture wicking into the PCB edge is the usual failure mode.
- Placement matters: readings are local to the probe. Keep depth/position consistent,
  a few cm from the pot edge, not touching the root ball or the pot wall.

## 3. Calibration (per sensor — boards vary unit to unit)

1. `raw_air`: reading with the probe in free, dry air.
2. `raw_water`: reading with the probe in a cup of water up to the insertion line.
3. Convert: `moisture% = 100 * (raw_air - raw) / (raw_air - raw_water)`, clamped 0–100.

- Typical 12-bit values at 3.3 V: dry air ~2600–3100, water ~1300–1700. Exact numbers
  don't matter — the **delta** does (should be several hundred counts; if not, see
  the clone caveat above).
- Make calibration button-driven on the device (see UI below) and persist per-sensor
  values (FlashStorage_SAMD library, or a config file on the SD card).
- This yields a **relative** moisture index, not absolute volumetric water content.
  That's fine: watering decisions and drying rate only need consistency, not accuracy.
- Expect mild temperature drift and day/night oscillation — another reason to work
  with smoothed trends rather than single readings.

## 4. Firmware plan (Arduino / PlatformIO)

- Toolchain: Arduino IDE with Seeed SAMD board package, or PlatformIO with
  `board = seeed_wio_terminal`. LCD via `TFT_eSPI` (bundled with the Seeed core);
  SD via `Seeed_FS`. Plain `millis()` scheduling — no RTOS needed.

### Sampling
- `analogReadResolution(12)` in `setup()`.
- One "reading" = **median of ~15 raw samples** taken 2 ms apart (kills ADC jitter
  and EMI from the LCD/WiFi).
- Take a reading every 30 s for the display; commit one sample to the history buffer
  every 5 min (soil changes slowly — oversampling the log adds noise, not info).

### Data model
- Ring buffer of `(timestamp, moisture%)`: 576 entries = 48 h at 5-min cadence
  (~4.6 KB — trivial for the SAMD51's 192 KB RAM).
- Exponential moving average (alpha ≈ 0.2) over the logged samples for display and
  rate estimation.

### Drying-rate estimation
1. **Watering detection:** a jump of more than ~ +8 moisture-points between
   consecutive smoothed samples = watering event. Record it and reset the trend
   window (drying statistics must not straddle a watering discontinuity).
2. **Rate (v1):** ordinary least-squares slope over the trailing 6–12 h window
   → drying rate in %/hour. Simple, robust, good enough.
3. **ETA to watering:** `hours_left = (moisture_now - threshold) / |slope|`,
   threshold ≈ 30–40% depending on the plant. Display as "water in ~2.3 days".
4. **Rate (v2, optional):** soil drying is closer to exponential decay toward an
   air-dry asymptote: `m(t) = m_res + (m0 - m_res)·e^(−t/τ)`. Fitting τ after each
   watering gives a better long-horizon ETA and lets you compare pots/seasons.
   Do the linear version first; upgrade only if ETAs are visibly off.

### UI / outputs
- LCD: current % (big), drying rate, ETA, 24–48 h sparkline; one page per sensor.
- Buttons: A = calibrate-in-air, B = calibrate-in-water, C = cycle sensor/plant.
- Buzzer chirp + on-screen alert when below threshold.
- Optional: CSV logging to microSD (RTC timestamps), or WiFi → MQTT/Home Assistant.

## 5. Milestones

1. **Bring-up:** one sensor on A0, minimal sketch printing raw ADC to Serial
   (115200). Verify a large dry-air vs in-water delta. *(Catches wiring and the
   clone-at-3.3V issue before writing anything else.)*
2. **Calibration + display:** button-driven 2-point calibration, persisted;
   moisture % on the LCD.
3. **Trends:** ring buffer, EMA, watering detection, least-squares drying rate,
   ETA on screen.
4. **Scale-out (optional):** remaining sensors on A1–A4, per-plant pages and
   thresholds, SD logging, buzzer alerts, WiFi/MQTT.

## 6. Risks & gotchas checklist

- [ ] Never feed a 5 V-powered sensor's AOUT into the Wio Terminal without measuring
      it first — SAMD51 GPIO is not 5 V tolerant.
- [ ] Cable wire colors may not match sensor pin order — continuity-check first.
- [ ] Per-sensor calibration required; recheck monthly (probe coating ages).
- [ ] Keep the sensor's electronics area dry; seal the board top for long-term use.
- [ ] Fertilizer/salinity and temperature shift readings — trust trends over
      absolute values.
- [ ] Wet/dry extremes saturate: the sensor can't distinguish "soaked" from
      "standing water", nor "dry" from "bone dry".

## References

- Seeed Wio Terminal Getting Started (pinout diagrams, specs):
  <https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/>
- Pin definitions in the Seeed Arduino core (authoritative A0–A8 mapping):
  <https://github.com/Seeed-Studio/ArduinoCore-samd/tree/master/variants/wio_terminal>
- Grove-port exploration write-up (confirms A0=PB08/BCM27, A1=PB09/BCM22):
  <https://smist08.wordpress.com/2021/10/15/playing-with-the-wio-terminals-grove-connectors/>

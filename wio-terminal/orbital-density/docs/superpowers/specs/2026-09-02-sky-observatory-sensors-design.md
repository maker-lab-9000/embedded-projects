# Sky Observatory Sensor Integration — Design

**Date:** 2026-09-02
**Status:** approved in chat (decisions 1–6 below); implementation started 2026-09-03
**Source concept:** `wio-sky-observatory-integration.md` (the "multi-layer sky observatory" doc)
**Target firmware:** `firmware/m2_skyview/` on the Seeed Wio Terminal (`Seeeduino:samd:seeed_wio_terminal`)

## 1. Goal and scope

Add three I²C sky sensors to the existing Orbital Density firmware and turn its
per-minute GPS log into a 1 Hz, GNSS-timestamped observation record.

| Sensor | Layer | Bus address | Board form |
|---|---|---:|---|
| Adafruit TSL2591 | optical sky brightness (visible + near-IR) | `0x29` | STEMMA QT |
| Adafruit MMC5603 | magnetic vector, field magnitude, heading | `0x30` | STEMMA QT |
| MLX90614ESF-BCC on a GY-906 module | long-wave IR sky brightness temperature | `0x5A` | 0.1" header pins — **deferred** (needs soldering) |

**Decisions taken (2026-09-02):**

1. **Keep the existing BME280** for temperature, humidity and pressure. The SHT40 and the separate pressure sensor from the concept doc are out of scope.
2. **The main sketch may be split into several files** in `firmware/m2_skyview/`. The AGENTS.md "avoid broad refactors" rule is relaxed to: existing GPS parsing, sky plot, chart and logging code is left in place; new functionality lives in new header files; shared plumbing (page table, loop, setup) receives small, targeted edits.
3. **Observation log runs at 1 Hz** into a new `/obs.csv`. Lower the rate later only if SD write time or file size becomes a problem. `/gps.csv` keeps its schema and 60 s cadence.
4. **The MLX90614 is a GY-906 module**, wired with a Grove-to-female-jumper cable.
5. **The 650 mAh battery chassis is part of the instrument.** The Air530Z moves from header jumper wires to the chassis Grove UART socket (still `Serial1`). The hub on the LEFT Grove port carries BME280, TSL2591 and MMC5603, one per socket; the chassis I²C socket stays free.
6. **(2026-09-03) The GY-906 / MLX90614 is deferred** until its header is soldered. The firmware carries a compile-time `MLX_ENABLED` flag (default `0`): no driver compiled, its `/obs.csv` columns stay in the schema and are written empty, and the pages show "not installed". Flipping the flag to `1` and running the two deferred tasks brings it in with no other change.

**Out of scope for now:** MLX90614 (deferred, §4.7), SHT40, extra pressure sensor, dust sensor (temporarily excluded by the concept doc; see §4.6), Wi-Fi/MQTT export, a real cloud-index classifier (only a provisional Δ-sky threshold is displayed until Phase 5 data exists), enclosure design.

## 2. Hardware and wiring

### 2.1 Bus topology

The Wio Terminal exposes one I²C Grove port, the **left** one labelled SDA/SCL
(`Wire`, SERCOM3, PA16/PA17). The 650 mAh battery chassis (attached) adds six
Grove sockets fed from the 40-pin header: one I²C (same `Wire` bus), one UART
(the header's only UART, TXD/RXD pins 8/10 = `Serial1`) and four analog/digital.
The Grove I²C Hub is passive: its four sockets are wired in parallel, one is the
uplink, three take devices: BME280, TSL2591, MMC5603.

Chassis socket labels (silkscreen on the back of the chassis board) and what they reach,
from Seeed's schematic (`WioTerminal_battry_650mAh.rar`, Eagle refs in brackets):

| Label on the chassis | Grove pin 1 / pin 2 | Header pins | Arduino names |
|---|---|---:|---|
| `RX TX` (bottom edge, beside the USB-C) [J5] | UART1_RXD / UART1_TXD | 10 / 8 | `Serial1` RX / TX |
| `SCL SDA` (bottom edge, other side of the USB-C) [J6] | I2C1_SCL / I2C1_SDA | 5 / 3 | `Wire` SCL / SDA |
| `IO0 IO1` (side) [J1] | D0/A0, D1/A1 | 13 / 15 | `D0`, `D1` |
| `IO2 IO3` (side) [J3] | D2/A2, D3/A3 | 16 / 18 | `D2`, `D3` |
| `IO4 IO5` (side) [J4] | D4/A4, D5/A5 | 22 / 32 | `D4`, `D5` |
| `IO6 IO8` (side) [J2] | D6/A6, D8/A8 | 33 / 37 | `D6`, `D8` |

All six sockets look identical; only the label differs. The `IO*` pins have no SERCOM,
so a UART module in one of them is silent on `Serial1`. This happened on 2026-09-03 (GPS
in `IO4 IO5`); `firmware/m1_pinsweep` found its TX toggling on header pin 22 within seconds.

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

Deferred: GY-906 / MLX90614 (0x5A) via Grove-to-female-jumper cable into the spare
chassis I²C socket once its header is soldered (same `Wire` bus, nothing else moves).
```

Every sensor has its own socket, so any one can be unplugged without disturbing
the others. With only one Grove-to-QT cable on hand, the MMC5603 can instead be
chained from the TSL2591's second STEMMA QT connector (QT-to-QT cable); the bus
does not care.

### 2.2 GNSS stays on `Serial1`, now via the chassis UART socket

The Air530Z keeps talking to `Serial1` (SERCOM2, PB26/PB27 = header pins 8/10);
no firmware change. Physically it moves from jumper wires on the header to the
chassis Grove UART socket, which is fed from those same header pins because the
header has exactly one UART. Confirmed from the chassis schematic (J5 → UART1_RXD/TXD →
header 10/8) and by the Task 1 sentence count. The old jumper wiring stays documented as
a fallback.

The right Grove port (D0/D1 = PB08/PB09) is not an option for this firmware,
for three stacked reasons: the core declares those pins `PIO_ANALOG`, so a
`Uart` leaves them in analog mode unless `pinPeripheral(…, PIO_SERCOM_ALT)` is
also called; their only SERCOM is SERCOM4, whose TX must sit on pad 0 = D0, the
opposite of the Grove UART convention (signals would have to be swapped in the
cable); and the core's `Wire.cpp` already defines the four `SERCOM4_x_Handler`
interrupt handlers for `Wire1`, so any sketch that uses `Wire` and also defines
them fails to link. Escaping that means editing the core's variant file.

### 2.3 Full address map on `Wire`

| Address | Device | Status |
|---:|---|---|
| `0x28` | reserved (TSL2591 internal) | keep free |
| `0x29` | TSL2591 | new |
| `0x30` | MMC5603 | new |
| `0x55` | BQ27441 fuel gauge (battery chassis) | existing |
| `0x5A` | MLX90614 | deferred (GY-906 not yet soldered) |
| `0x77` | BME280 (hard-coded in `m2_skyview`) | existing |

The BQ27441 sits on the chassis and reaches `Wire` through the header whether or
not the chassis I²C socket is used; the socket itself is a passive pass-through.

### 2.4 Electrical notes

- Grove VCC is **3.3 V**. All three sensors are 3.3 V parts; the `-BCC` suffix is the 3 V, 35° field-of-view, gradient-compensated MLX90614.
- Grove I²C pin order: pin 1 yellow SCL, pin 2 white SDA, pin 3 red VCC, pin 4 black GND. Map to GY-906 pins SCL, SDA, VIN, GND.
- If the GY-906 carries an LDO it will still run from 3.3 V in; the MLX90614 accepts 2.6–3.6 V. Verify the sensor rail with a meter if readings are unstable.
- **Bus clock stays at 100 kHz** even while the MLX90614 is deferred: it is SMBus and tops out there, and the TSL2591/MMC5603 lose nothing at 100 kHz. Set it explicitly in `setup()` with a comment so nobody raises it later.
- **Pull-ups stack.** Each Adafruit board adds 10 kΩ, the GY-906 typically 4.7 kΩ, the BME280 board its own. Combined value may approach 1.5 kΩ, which is at the edge of the I²C sink spec at 3.3 V. Normally fine at 100 kHz; if the boot scan misses devices, remove one board's pull-ups.
- The chassis sockets are plain pass-throughs of header pins: no extra pull-ups, no level shifting, same 3.3 V rail.
- **Magnetometer placement.** MMC5603 sits at the end of a 15–30 cm non-magnetic arm, away from the speaker magnet, battery, SD card and display. The QT cable to it runs down the arm.

### 2.5 Parts list

- Grove I²C Hub (4 sockets), plus one Grove-to-Grove cable for the uplink
- Adafruit 4528 Grove-to-STEMMA QT cable ×2 (TSL2591, MMC5603); with only one, add a
  STEMMA QT-to-QT cable and chain the MMC5603 from the TSL2591 instead
- One plain Grove cable: Air530Z → chassis UART socket (the BME280 keeps its own)
- Later, for the GY-906: the Grove-to-female-jumper cable the GPS used until now

## 3. Constraints discovered in the current code and libraries

- **Serial RX buffer is 256 bytes** (`SERIAL_BUFFER_SIZE` in the Seeed SAMD core). At 115200 baud it fills in ~22 ms. Anything that blocks the loop longer than that drops NMEA bytes. The existing 1 Hz `drawPage()` sprite blit is already in that range and the firmware tolerates it through checksum rejection, so the metric to protect is the NMEA checksum-failure rate, not a hard ms limit.
- **Adafruit_TSL2591 blocks.** `getFullLuminosity()` calls `delay(120)` × (integration steps + 1): 720 ms at the 600 ms integration the night sky needs. `getLuminosity()` wraps it. Not usable in this loop. We write a small register-level driver instead (same style as the existing BME280 code).
- **Adafruit_MMC56x3 blocks in one-shot mode** (`while (!done) delay(5)`). In continuous mode `getEvent()` just reads registers. We use continuous mode.
- **Adafruit_MLX90614** reads are single SMBus word transfers, well under 1 ms each. Usable as-is.
- **SD `open`/`printf`/`close` per row** is fine at 60 s but not at 1 Hz. The observation file stays open and is `flush()`ed every 10 s.
- **RAM:** 77 KB sprite + ~8 KB histories of 192 KB. New 288-sample `int16_t` histories cost 576 bytes each. Budget: at most four new histories.
- **CPU:** `computeBodies()` (soft-float `double` ephemeris) is the heaviest 1 Hz job. New per-second work is a few register reads; new per-loop work is one `millis()` compare and one status-register read.
- **Fallback if NMEA loss grows:** raise the UART buffer with a build property, e.g. `arduino-cli compile --build-property "compiler.cpp.extra_flags=-DSERIAL_BUFFER_SIZE=1024" ...`. Not baseline; only if the counter in §7 shows a regression.

## 4. Firmware architecture

### 4.1 File layout (single translation unit, header-only modules)

Arduino compiles `m2_skyview.ino` and includes headers from the same folder.
Each module is a header that defines its own globals and functions, included
exactly once from the sketch after the shared globals (`gps`, `spr`) exist.
This keeps the existing single-TU style (auto-prototypes, shared globals)
while giving each sensor its own file.

```
firmware/m2_skyview/
  m2_skyview.ino          existing sketch; edits limited to includes, setup(), loop(),
                          page dispatch, CSV assembly hooks
  ring.h                  Ring<T,N>: generic 288-sample ring buffer (new histories only)
  i2c_bus.h               i2cPresent(), i2cScan() (boot only), Wire clock constant
  tsl2591.h               register-level, non-blocking TSL2591 driver
  mmc5603.h               MMC5603 via Adafruit_MMC56x3, continuous mode
  mlx90614.h              MLX90614 via Adafruit_MLX90614 (deferred; compiled only with MLX_ENABLED 1)
  observation.h           Observation struct, assembleObservation(), obs.csv column table
  pages_sensors.h         drawSkySensors(), drawMag(), drawSensors(), drawSeries() helper
  loopstats.h             per-iteration max loop time; NMEA failures come from TinyGPS++'s failedChecksum()
```

### 4.2 Module contract

Every sensor module exposes the same shape so the loop and the observation
assembler stay uniform:

```cpp
bool     xxxOk;                 // present and initialised
bool     xxxInit();             // probe + configure; returns ok; safe to call again
void     xxxPoll();             // called every loop iteration; never blocks; uses millis()
uint32_t xxxLastMs;             // millis() of the last good sample
// plus module-specific value globals, e.g. tslFull, tslIr, tslLux ...
```

`xxxPoll()` decides internally whether it is time to talk to the device. Missing
sensors are re-probed every 30 s from the loop (`if (!xxxOk) xxxInit();`), so
hot-plugging works and one dead sensor never stops the others.

### 4.3 Observation

Assembled once per second in the existing 1 Hz block, after the sensors have
polled, and handed to the display and the logger.

```cpp
struct Observation {
  char     utc[24];          // "YYYY-MM-DDTHH:MM:SSZ" or "" until first fix
  uint32_t uptimeS;
  // GNSS
  bool fixValid; double lat, lon; float altM, hdop; uint8_t used, inView; const char* fix;
  // optical (TSL2591)
  bool tslOk; uint16_t tslFull, tslIr; uint8_t tslGainIdx; uint16_t tslIntegMs; float tslLux; bool tslSat;
  // thermal (MLX90614)
  bool mlxOk; float mlxAmbC, mlxObjC, mlxDeltaC;
  // magnetic (MMC5603), 1 s means of the 10 Hz stream
  bool magOk; float bx, by, bz, bTotal, headingDeg;
  // atmosphere (BME280)
  bool bmeOk; float tempC, hum, presHpa;
};
```

Sub-second timestamps are not attempted: there is no RTC, GNSS gives whole
seconds, and a 1 Hz log does not need more. `uptime_s` remains the fallback
clock before the first fix.

### 4.4 CSV from one column table

Header and row are generated from a single static array so they cannot drift
(the existing `/gps.csv` maintains them as two hand-edited literals).

```cpp
struct Col { const char* name; void (*fmt)(char* out, size_t n, const Observation& o); };
const Col OBS_COLS[] = {
  {"utc",        [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%s", o.utc); }},
  {"uptime_s",   [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%lu", (unsigned long)o.uptimeS); }},
  ...
};
```

`/obs.csv` columns, in order: `utc, uptime_s, lat, lon, alt_m, fix, hdop, used,
in_view, tsl_full, tsl_ir, tsl_vis, tsl_gain, tsl_integ_ms, tsl_lux, tsl_sat,
mlx_ambient_c, mlx_object_c, mlx_delta_c, mag_x_ut, mag_y_ut, mag_z_ut,
mag_total_ut, heading_deg, temp_c, humidity, pressure_hpa, sensors_ok`.
`sensors_ok` is a bitmask (bit0 TSL, bit1 MLX, bit2 MAG, bit3 BME). Empty
fields are written as empty strings, never as zeros, when a sensor is absent.
About 180 bytes per row, roughly 15 MB per day.

### 4.5 Page table and navigation

The `page = (page + 1) % 6` and the ternary chain in `drawPage()` are replaced
by one table:

```cpp
typedef void (*PageFn)();
struct PageDef { const char* name; PageFn draw; };
const PageDef PAGES[] = { {"Sky", drawSky}, {"Detail", drawDetail}, {"Chart", drawChart},
#if DUST_ENABLED
  {"Dust", drawDust},
#endif
  {"Env", drawEnv}, {"Obs", drawObs},
  {"SkySens", drawSkySensors}, {"Mag", drawMag}, {"Sensors", drawSensors} };
```

Navigation: top button `WIO_KEY_C` keeps cycling forward (unchanged
behaviour); 5-way **right** = next page, **left** = previous page; centre
press still toggles the backlight. Up/down are reserved for a later
"rotate sky plot by heading" toggle.

### 4.6 Dust sensor gate

`#define DUST_ENABLED 0` at the top of the sketch compiles out `pollDust()`,
the Dust page and the dust history. With the sensor unplugged the D0 input
floats and would log garbage; with the define off it logs empty fields in
`/gps.csv`. Flip to `1` to restore the current behaviour. When the sensor returns it goes in the
chassis `IO0 IO1` socket, whose pin 1 is `D0`, so `DUST_PIN` stays as it is.

### 4.7 MLX90614 gate (deferred sensor)

`#define MLX_ENABLED 0` in the sketch. While `0`: `mlx90614.h` is not included, no
init/poll/re-probe, `assembleObservation()` writes `mlxOk = false` and `NAN` values so
the `mlx_*` columns exist but are empty, the SkySens page prints "Sky IR   not
installed" and the condition label is `--`, and the Sensors page omits the row. Set to
`1` (after soldering the GY-906 and running the deferred bring-up) to compile the
module and all its page sections; the CSV schema does not change.

## 5. Sensor drivers

### 5.1 TSL2591 (register level, non-blocking)

Registers (command byte `0xA0 | reg`): `ENABLE 0x00` (PON `0x01`, AEN `0x02`),
`CONTROL 0x01` (AGAIN bits 5:4, ATIME bits 2:0), `ID 0x12` (= `0x50`),
`STATUS 0x13` (AVALID bit 0), `C0DATAL..C1DATAH 0x14..0x17`.

- **Init:** check ID, write CONTROL with the starting gain/integration (MED gain, 300 ms: a safe middle that auto-gain moves from within a few seconds in either daylight or darkness), write ENABLE = PON|AEN. The ALS then runs continuously; data registers refresh every integration period.
- **Poll:** when `millis() - tslLastMs >= tslIntegMs + 20` and STATUS.AVALID is set, burst-read 4 bytes from `0x14` (CH0 low/high then CH1 low/high, so channel 0 is read first). Store raw, current gain index, integration ms, `tslSat`, and lux.
- **Lux:** Adafruit's formula: `cpl = (atime_ms * again) / 408.0; lux = (ch0 - ch1) * (1 - ch1/ch0) / cpl`, `NAN` when `ch0 == 0` or saturated. Gain factors 1, 25, 428, 9876.
- **Saturation:** `ch0 >= 36863` at 100 ms, `ch0 == 0xFFFF` otherwise.
- **Auto-gain state machine:** if saturated or `ch0 > 0.9 × full scale`, step gain down (then integration down to 100 ms at LOW); if `ch0 < 200` counts, step integration up to 600 ms then gain up. After any change write CONTROL and set `tslSettling = true`; discard the next valid sample. Every sample records the settings that produced it, which is what the concept doc asks for.

### 5.2 MMC5603 (Adafruit_MMC56x3, continuous mode)

- **Init:** `mmc.begin(0x30, &Wire)`, `mmc.setDataRate(10)`, `mmc.setContinuousMode(true)`, then a raw write of CTRL0 (`0x1B`) = `0x20` to enable **automatic set/reset**. The datasheet recommends it; without it the raw bridge offset is specified at up to ±1 G (±100 µT) per axis, and the Adafruit library never sets it (its `reset()` leaves the sensor in RESET polarity, output = −H + offset). Bring-up on 2026-09-03 read a steady 173 µT that was pure offset.
- **Poll:** every 100 ms `mag.getEvent(&e)` (register read only in continuous mode). Apply hard-iron offsets `MAG_OFF_X/Y/Z` (constants, default 0, filled in Phase 5). Accumulate for the 1 s mean. Push `bTotal × 10` into a `Ring<int16_t,288>` every 5 min for the 24 h chart.
- **Set/Reset:** handled on-chip by auto set/reset; no periodic `magnetSetReset()` (it would flip polarity).
- **Heading:** `atan2(y, x)` in degrees, normalised to 0–360, plus `MAG_MOUNT_OFFSET_DEG` and `MAG_DECLINATION_DEG` constants (default 0, documented). Valid only when the instrument is level; the built-in LIS3DHTR on `Wire1` is the obvious future tilt source.

### 5.3 MLX90614 (Adafruit_MLX90614) — deferred, see §4.7

- **Init:** `mlx.begin(0x5A, &Wire)`; then one ambient read must return a plausible value (−40…85 °C) for `mlxOk`.
- **Poll:** at 1 Hz read `readAmbientTempC()` and `readObjectTempC()`, compute `mlxDeltaC = ambient − object`. `NAN` from the library marks the sample invalid. Label the object value **IR sky brightness temperature** everywhere in UI and docs.
- Push `mlxDeltaC × 10` into a `Ring<int16_t,288>` every 5 min.

### 5.4 Presence and scanning

`i2c_bus.h` provides `bool i2cPresent(uint8_t addr)` (begin/endTransmission
== 0) and `i2cScan()` run once in `setup()`; the scan result is also
shown on the Sensors page. Each `xxxInit()` does its own ID or plausibility
check; presence alone does not set `xxxOk`.

## 6. Display

Three new pages, drawn into the existing 8-bit sprite with the existing
`setTextSize(1|2)` typography. A shared `drawSeries(const int16_t* v, int len,
int y0, int h, float scale, uint16_t colour)` helper draws the 24 h lines for
the new pages; the three existing chart renderers are not touched.

- **SkySens:** visible lux (or raw counts with gain when lux is NAN), IR fraction %, sky IR °C, Δ sky °C, TSL gain/integration, and a "Condition" line that reads `CLEAR / CLOUDY / --` from a placeholder threshold on Δ sky that is explicitly marked provisional until Phase 5 data exists.
- **Mag:** Bx By Bz in µT, |B|, heading, plus the 24 h |B| chart.
- **Sensors:** one row per I²C device: address, name, OK/missing, last-sample age; last boot scan result; `loopMax ms` and `nmeaBad` counters from §7.

## 7. Timing instrumentation and regression metric

`loopstats.h` measures `micros()` per loop iteration and keeps the maximum
over the last second (`loopMaxMsLast`). NMEA health comes from TinyGPS++'s
built-in `gps.failedChecksum()` / `gps.passedChecksum()` counters (it validates
every sentence, GSV included). Both appear in the 1 Hz serial status line and
on the Sensors page. Acceptance for every driver step: after five minutes with
all sensors running, `failedChecksum()` grows no faster than on the unmodified
firmware measured in Phase 1.

## 8. Logging

- `/obs.csv` at 1 Hz (`OBS_PERIOD_MS = 1000`), file held open, `flush()` every 10 s, header written from `OBS_COLS` when the file is created.
- `/gps.csv` unchanged.
- SD errors set `sdOk = false` and the existing badge shows `SD!`; `initSd()` retries on the next row as today.
- Optional, later: one JSON line per second on USB serial for a PC capture script.

## 9. Bring-up sketches (Phase 0)

Following the existing `firmware/m1_*` convention, one serial-only sketch per
new device plus a bus scanner:

- `firmware/m1_i2c_scan/` – scan and print every 5 s (promoted from `m1_bme280`'s `i2cScan()`), plus a count of NMEA sentences arriving on `Serial1`, which confirms the chassis UART socket in the same run.
- `firmware/m1_pinsweep/` – counts edges on every 40-pin-header GPIO for 1 s; tells which header pin a chassis-socket signal actually lands on (found the mis-socketed GPS).
- `firmware/m1_tsl2591/` – register driver, prints raw CH0/CH1, gain, integration, lux at 1 Hz.
- `firmware/m1_mmc5603/` – continuous-mode library read, prints X/Y/Z/|B|/heading at 2 Hz.
- `firmware/m1_mlx90614/` – prints ambient, object, delta at 1 Hz (deferred until the GY-906 is soldered).

Libraries to install now: `Adafruit MMC56x3` (`Adafruit BusIO` and `Adafruit Unified
Sensor` are already present); `Adafruit MLX90614 Library` when the GY-906 arrives. No
TSL2591 library.

## 10. Testing and verification

There is no automated test suite. Gates, in order:

1. `arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal firmware/<sketch>` passes for every touched sketch.
2. Bring-up sketch prints sane values for each sensor **with all devices on the hub at once**.
3. Unmodified `m2_skyview` plus `loopstats.h` gives a baseline `loopMaxMs` and `nmeaBadCount` rate (5 min).
4. Each driver added in turn keeps the `nmeaBadCount` rate within baseline.
5. Page navigation on hardware; screenshots for README.
6. `/obs.csv` on the card: header matches `OBS_COLS`, one row per second, empty fields for absent sensors, file survives power-cycle.

Hardware steps (plugging, pointing, reading the screen, pulling the card) are the user's; the plan stops and asks at those points.

## 11. Phase 5: field calibration (after the firmware phases)

- Arm-length test: log |B| while toggling backlight, SD writes and Wi-Fi at 15, 20, 25, 30 cm; pick the shortest arm with stable |B|.
- Hard-iron: rotate the instrument slowly through 360°, fit offsets, set `MAG_OFF_*`.
- Optical and thermal baselines under clear, cloudy and moonlit skies before replacing the provisional Condition threshold.

## 12. Documentation updates

- README: chassis + hub wiring section (GNSS on the chassis UART socket with the header jumpers kept as a fallback, BME280/TSL2591/MMC5603 on the hub, GY-906 deferred), address map, cable list, `/obs.csv` schema, new page descriptions and screenshots, fix the 0x76/0x77 auto-detect claim (main firmware is hard-coded to 0x77).
- AGENTS.md: replace the blanket "avoid broad refactors" sentence with the rule in decision 2; add the new libraries to the required list.
- Concept doc: note that Galileo is not receivable on this module and that the SHT40/pressure rows are superseded by the BME280.

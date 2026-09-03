# Sky Observatory Sensors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the TSL2591 (optical), MMC5603 (magnetic) and MLX90614 (thermal IR) sensors to the Orbital Density Wio Terminal firmware over a Grove I²C hub, with non-blocking drivers, three new display pages and a 1 Hz GNSS-timestamped `/obs.csv` observation log.

**Architecture:** The existing single-file sketch `firmware/m2_skyview/m2_skyview.ino` stays the main translation unit; every new module is a header in the same folder that defines its own globals and functions (`xxxOk`, `xxxInit()`, `xxxPoll()`), included once from the sketch. Sensors are polled from the cooperative `loop()` with `millis()` gates and never block; an `Observation` struct is assembled once per second and written to `/obs.csv` from a single column table that generates both header and rows. Each sensor is first proven in a self-contained `firmware/m1_*` serial bring-up sketch, matching the repo's existing convention.

**Tech Stack:** arduino-cli, `Seeeduino:samd` core 1.8.6 (bundled `TFT_eSPI`, `Wire`), `TinyGPSPlus`, `Seeed Arduino FS`, `SparkFun BQ27441`, new: `Adafruit MMC56x3`, `Adafruit MLX90614 Library` (both pull in the already-installed `Adafruit BusIO` and `Adafruit Unified Sensor`). TSL2591 is driven by a hand-written register-level header (the Adafruit library blocks).

**Spec:** `docs/superpowers/specs/2026-09-02-sky-observatory-sensors-design.md` (read it first; this plan argues from it).

## Global Constraints

- FQBN (verbatim): `Seeeduino:samd:seeed_wio_terminal`. Compile command, run from the repo root `/Users/george.babanau/repos/embedded`: `arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/<sketch>`. A clean compile is the gate for every task.
- Upload: `arduino-cli upload --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodemXXX wio-terminal/orbital-density/firmware/<sketch>`; find the port with `arduino-cli board list`. Close serial monitors first.
- I²C: hub on the **LEFT** Grove port (`Wire`, SERCOM3). Bus clock **100 kHz, never higher** (MLX90614 is SMBus). Supply 3.3 V. Addresses: `0x29` TSL2591, `0x30` MMC5603, `0x55` BQ27441, `0x5A` MLX90614, `0x77` BME280; `0x28` reserved.
- Chassis: the 650 mAh battery chassis is attached. Its Grove UART socket = header pins 8/10 = `Serial1`: the Air530Z plugs in there, code unchanged. Its Grove I²C socket (same `Wire` bus) stays spare for the GY-906 later. The three hub sockets carry BME280, TSL2591 and MMC5603, one each.
- MLX90614 / GY-906 is **deferred** (header not soldered): `#define MLX_ENABLED 0` in the sketch (introduced in Task 10). Tasks 4 and 9 are skipped until it arrives; `/obs.csv` keeps its `mlx_*` columns, written empty.
- Never put a UART on the right Grove port (D0/D1): SERCOM4's interrupt handlers belong to `Wire1` in the core, TX would land on the wrong Grove pin, and the pins default to the analog mux.
- Loop budget: the UART RX buffer is 256 bytes (~22 ms at 115200 baud). No new code may block. Regression metric: `gps.failedChecksum()` growth per 5 min must not exceed the Task 5 baseline; `loopMaxMs` is displayed for diagnosis.
- Existing behaviour of GPS parsing, sky plot, existing charts and `/gps.csv` is preserved. Edits to `m2_skyview.ino` are limited to: includes, `#define DUST_ENABLED`, `setup()`, `loop()`, `pollButtons()`, `drawPage()`, `logRow()` dust fields, and the new observation-log block.
- `Serial.printf()` on this core truncates at about 80 characters: keep each call short and build long lines from several calls.
- Style: two-space indent, `camelCase` functions/variables, `UPPER_SNAKE` constants, hardware assumptions in comments next to the code (see `AGENTS.md`).
- Hardware steps (plugging cables, uploading, reading the screen, pulling the SD card) are the user's. At every **Hardware checkpoint** stop, tell the user exactly what to do and what to expect, and wait.
- Commit after every task with an imperative subject prefixed `orbital-density:` (docs: `orbital-density docs:`). Do **not** push unless the user asks.

## File Structure

```
wio-terminal/orbital-density/
  firmware/m1_i2c_scan/m1_i2c_scan.ino      Task 1  bus scan with device names + Serial1 NMEA count (confirms the chassis UART socket)
  firmware/m1_pinsweep/m1_pinsweep.ino       Task 1  header-pin edge counter: which header pin is a chassis-socket signal on?
  firmware/m1_tsl2591/tsl2591.h              Task 2  register-level non-blocking TSL2591 driver (authored here)
  firmware/m1_tsl2591/m1_tsl2591.ino         Task 2  serial bring-up
  firmware/m1_mmc5603/m1_mmc5603.ino         Task 3  serial bring-up, continuous mode, read-time measurement
  firmware/m1_mlx90614/m1_mlx90614.ino       Task 4  DEFERRED — serial bring-up, read-time measurement
  firmware/m2_skyview/loopstats.h            Task 5  loop max-time window
  firmware/m2_skyview/i2c_bus.h              Task 6  clock constant, presence probe, boot scan
  firmware/m2_skyview/tsl2591.h              Task 7  copy of the Task 2 driver
  firmware/m2_skyview/ring.h                 Task 8  Ring<T,N> history buffer
  firmware/m2_skyview/mmc5603.h              Task 8  magnetometer module
  firmware/m2_skyview/mlx90614.h             Task 9  DEFERRED — thermal module, compiled only with MLX_ENABLED 1
  firmware/m2_skyview/observation.h          Task 10 Observation struct + OBS_COLS table + header/row writers
  firmware/m2_skyview/pages_sensors.h        Task 12 drawSeries(), drawSkySensors(), drawMag(), drawSensors()
  firmware/m2_skyview/m2_skyview.ino         Tasks 5–12 targeted edits
  tools/mag_calib.py                         Task 14 hard-iron offsets from /obs.csv
  README.md, AGENTS.md, wio-sky-observatory-integration.md   Task 13
```

Line numbers below refer to `m2_skyview.ino` **as of commit `e1c0557`** (1258 lines). Re-locate by the quoted code if earlier tasks have shifted them.

---

### Task 1: Libraries, re-cabling onto the chassis + hub, and the `m1_i2c_scan` bring-up sketch

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m1_i2c_scan/m1_i2c_scan.ino`
- Create: `wio-terminal/orbital-density/firmware/m1_pinsweep/m1_pinsweep.ino` (diagnostic, see Step 4)

**Interfaces:**
- Produces: confirmation that all four devices answer on `Wire` at 100 kHz, that the Air530Z is heard on `Serial1` through the chassis UART socket, and the Adafruit MMC56x3 library installed for Tasks 3 and 8.

- [x] **Step 1: Install the magnetometer library**

```bash
arduino-cli lib install "Adafruit MMC56x3"
arduino-cli lib list | grep -E "MMC56x3|BusIO|Unified Sensor"
```

Expected: three lines, one per library (BusIO and Unified Sensor were already present). The MLX90614 library is installed in the deferred Task 4.

- [x] **Step 2: Write the scanner sketch**

Create `wio-terminal/orbital-density/firmware/m1_i2c_scan/m1_i2c_scan.ino`:

```cpp
// Milestone 1 — I2C bus scan + GNSS UART check for the sky-observatory re-cabling.
//
// WIRING (Wio Terminal in the 650 mAh battery chassis):
//   chassis socket labelled RX TX (beside the USB-C) -> Grove cable -> Air530Z GNSS  (= header 10/8 = Serial1)
//   the four IO* sockets look identical but have no UART: a GPS there is silent on Serial1
//   chassis Grove I2C socket  -> spare (GY-906 / MLX90614 goes here once its header is soldered)
//   LEFT Grove port (SDA/SCL) -> Grove cable -> Grove I2C Hub (passive, 4 sockets in parallel)
//     hub socket -> Grove cable -> BME280
//     hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> TSL2591
//     hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> MMC5603 (or chain it from the TSL2591)
// Expected: 0x29 TSL2591  0x30 MMC5603  0x55 BQ27441 (chassis)  0x77 BME280  (0x5A MLX90614 later),
// plus a non-zero NMEA sentence count from Serial1 every 5 s.
// Bus is 100 kHz because the MLX90614 is an SMBus part. Grove VCC is 3.3 V.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>

uint32_t nmeaCount = 0, byteCount = 0;   // '$' chars / all bytes seen on Serial1 since the last report
char     peek[61]; uint8_t peekLen = 0;  // first printable chars of the period, to eyeball NMEA vs garbage
uint32_t gpsBaud = 115200, gpsProbeStart = 0, gpsSwitches = 0;
bool     gpsSawNmea = false;

void gpsSetBaud(uint32_t b) { Serial1.begin(b); gpsBaud = b; gpsProbeStart = millis(); gpsSawNmea = false; }

// Called every loop(): if nothing valid arrives for 2.5 s, try the other baud. When NMEA shows
// up at 9600, send $PCAS01,5 (module -> 115200) and re-listen at 115200; count the attempts.
void gpsAutoBaud() {
  if (gpsSawNmea && gpsBaud == 9600) {
    Serial1.println("$PCAS01,5*19");
    Serial1.flush();
    delay(100);
    gpsSwitches++;
    Serial.printf("GPS: NMEA seen at 9600 -> sent $PCAS01,5 (attempt %lu), listening at 115200\n", (unsigned long)gpsSwitches);
    gpsSetBaud(115200);
    return;
  }
  if (!gpsSawNmea && millis() - gpsProbeStart > 2500) {
    gpsSetBaud(gpsBaud == 115200 ? 9600 : 115200);
    Serial.printf("GPS: nothing valid, now listening at %lu\n", (unsigned long)gpsBaud);
  }
}

const char* nameFor(uint8_t a) {
  switch (a) {
    case 0x29: return "TSL2591 optical";
    case 0x30: return "MMC5603 magnetometer";
    case 0x55: return "BQ27441 fuel gauge (chassis)";
    case 0x5A: return "MLX90614 thermal IR";
    case 0x76: case 0x77: return "BME280 env";
    default:   return "unknown";
  }
}

void i2cScan() {
  Serial.println("--- I2C scan (Wire, 100 kHz) ---");
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X  %s\n", addr, nameFor(addr));
      found++;
    }
  }
  Serial.printf("--- %d device(s) ---\n", found);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);   // MLX90614 is SMBus: 100 kHz max for the whole bus

  // Air530Z on the chassis UART socket = Serial1. Auto-baud (see gpsSetBaud): the module
  // boots at 9600 unless it persisted 115200, and the SAMD core DROPS bytes with framing
  // errors, so listening at the wrong baud looks like "0 bytes", not garbage.
  gpsSetBaud(115200);
}

void loop() {
  static uint32_t lastReport = 0;
  while (Serial1.available()) {
    char c = Serial1.read(); byteCount++;
    if (c == '$') { nmeaCount++; gpsSawNmea = true; }
    if (peekLen < 60 && c >= 32 && c < 127) peek[peekLen++] = c;
  }
  gpsAutoBaud();
  if (millis() - lastReport >= 5000) {
    lastReport = millis();
    i2cScan();
    peek[peekLen] = 0;
    Serial.printf("GPS on Serial1 @%lu: %lu bytes, %lu '$' in 5 s", (unsigned long)gpsBaud, (unsigned long)byteCount, (unsigned long)nmeaCount);
    if (byteCount == 0)      Serial.print("  <-- nothing: UART socket / cable / GPS power");
    else if (nmeaCount == 0) Serial.print("  <-- bytes but no '$': baud mismatch");
    Serial.println();
    if (peekLen) { Serial.print("  first chars: "); Serial.println(peek); }
    nmeaCount = byteCount = 0; peekLen = 0;
  }
}
```

- [x] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m1_i2c_scan
```

Expected: `Sketch uses ... bytes` with no errors.

- [x] **Step 4: Hardware checkpoint (user)**

Ask the user to re-cable as in the sketch header: Air530Z → the chassis socket labelled **`RX TX`** on the back of the chassis, beside the USB-C (unplug the jumper wires from header pins 8/10; the four `IO*` sockets look identical and are not UARTs); hub → LEFT Grove port; BME280, TSL2591 and MMC5603 on the three remaining hub sockets (the chassis I²C socket stays empty for the GY-906 later). Upload; open a serial monitor at 115200.

Expected every 5 s: the scan lists `0x29`, `0x30`, `0x55`, `0x77`, and `GPS on Serial1:` reports a non-zero count (typically dozens of sentences per 5 s once the module runs at 115200; single digits at 9600 are still a pass). If something is missing:
- `GPS on Serial1: 0 bytes`: the Grove cable sits in an `IO*` socket instead of `RX TX`, or the module has no power (check its LED). To prove which, flash `firmware/m1_pinsweep` (Step 4a): edges on header pin 13/15/16/18/22/32/33/37 mean the GPS is in an `IO*` socket. Fallback: the old header jumper wiring (README) still works;
- `GPS on Serial1: N bytes, 0 '$'`: baud mismatch; power-cycle the module and re-run;
- none of the hub devices: uplink cable in the wrong Grove port (use the LEFT one), or a Grove cable seated one pin off;
- `0x77` missing: BME280 Grove cable not seated in its hub socket;
- `0x30` only missing: its Grove-to-QT cable, or the QT-to-QT cable if chained from the TSL2591;
- intermittent: pull-up stacking; unplug one device to confirm, then remove that board's pull-ups.

Do not continue until all four addresses and a non-zero GPS count appear together.

- [x] **Step 4a (only if the GPS count is zero): header pin sweep**

`firmware/m1_pinsweep/m1_pinsweep.ino` sets every 40-pin-header GPIO to INPUT and counts edges for 1 s, every 3 s. A UART TX line carrying NMEA shows hundreds to thousands of edges in 1 Hz bursts. Flash it, read the reported header pin, and match it against the chassis socket table in the spec (§2.1). Then re-flash `m1_i2c_scan`.

- [x] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m1_i2c_scan wio-terminal/orbital-density/firmware/m1_pinsweep
git commit -m "orbital-density: add hub scan, GNSS UART check and header pin-sweep bring-up sketches"
```

---

### Task 2: TSL2591 non-blocking driver and `m1_tsl2591` bring-up

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m1_tsl2591/tsl2591.h`
- Create: `wio-terminal/orbital-density/firmware/m1_tsl2591/m1_tsl2591.ino`

**Interfaces:**
- Produces (used verbatim by Task 7 and later):
  - `bool tslOk;` `bool tslInit();` `void tslPoll();` (call every loop iteration)
  - `uint16_t tslFull, tslIr; uint8_t tslGainIdx; uint16_t tslIntegMs; float tslLux; bool tslSat;`
  - `uint32_t tslLastMs, tslSampleCount, tslErrCount;` `const char* tslGainName();`

- [x] **Step 1: Write the driver header**

Create `wio-terminal/orbital-density/firmware/m1_tsl2591/tsl2591.h`:

```cpp
// tsl2591.h — register-level, NON-BLOCKING driver for the Adafruit TSL2591 (I2C 0x29).
//
// Why not Adafruit_TSL2591? Its getFullLuminosity() sleeps 120 ms per integration step
// (720 ms at the 600 ms integration a dark sky needs). The Wio's 256-byte UART buffer
// overflows in ~22 ms at 115200 baud, so that would drop NMEA every second. Here the ALS
// runs continuously and tslPoll() only reads registers when a new result is due.
//
// Bus: Wire @ 100 kHz (shared with an SMBus MLX90614). Supply 3.3 V.
// Datasheet: command byte 0xA0|reg; ENABLE 0x00 (PON 0x01, AEN 0x02); CONTROL 0x01
// (AGAIN bits 5:4, ATIME bits 2:0); ID 0x12 = 0x50; STATUS 0x13 bit0 AVALID;
// C0DATAL..C1DATAH 0x14..0x17. Full scale 36863 at 100 ms, 65535 otherwise.
#pragma once
#include <Arduino.h>
#include <Wire.h>

const uint8_t TSL_ADDR        = 0x29;
const uint8_t TSL_CMD         = 0xA0;
const uint8_t TSL_REG_ENABLE  = 0x00;
const uint8_t TSL_REG_CONTROL = 0x01;
const uint8_t TSL_REG_ID      = 0x12;
const uint8_t TSL_REG_STATUS  = 0x13;
const uint8_t TSL_REG_C0DATAL = 0x14;
const uint8_t TSL_EN_PON      = 0x01;
const uint8_t TSL_EN_AEN      = 0x02;

const uint8_t  TSL_GAIN_BITS[4] = {0x00, 0x10, 0x20, 0x30};
const float    TSL_GAIN_X[4]    = {1.0f, 25.0f, 428.0f, 9876.0f};
const char*    TSL_GAIN_NAME[4] = {"low", "med", "high", "max"};
const uint16_t TSL_INTEG_MS[6]  = {100, 200, 300, 400, 500, 600};
const float    TSL_LUX_DF       = 408.0f;   // Adafruit's device factor

bool     tslOk = false;
uint8_t  tslGainIdx  = 1;      // start MED / 300 ms: auto-range moves from here within seconds
uint8_t  tslIntegIdx = 2;
uint16_t tslIntegMs  = 300;
uint16_t tslFull = 0, tslIr = 0;
float    tslLux = NAN;
bool     tslSat = false;
bool     tslSettling = true;   // discard the first result after any settings change
uint32_t tslLastMs = 0;        // millis() of the last register read
uint32_t tslSampleCount = 0;
uint32_t tslErrCount = 0;

static bool tslWrite8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TSL_ADDR);
  Wire.write(TSL_CMD | reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool tslRead(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(TSL_ADDR);
  Wire.write(TSL_CMD | reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(TSL_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void tslApplySettings() {
  tslIntegMs = TSL_INTEG_MS[tslIntegIdx];
  tslWrite8(TSL_REG_CONTROL, TSL_GAIN_BITS[tslGainIdx] | tslIntegIdx);
  tslSettling = true;
  tslLastMs = millis();
}

const char* tslGainName() { return TSL_GAIN_NAME[tslGainIdx]; }

// Probe + configure. Safe to call again later (re-probe); returns tslOk.
bool tslInit() {
  uint8_t id = 0;
  if (!tslRead(TSL_REG_ID, &id, 1) || id != 0x50) { tslOk = false; return false; }
  tslWrite8(TSL_REG_ENABLE, TSL_EN_PON);
  tslApplySettings();
  tslWrite8(TSL_REG_ENABLE, TSL_EN_PON | TSL_EN_AEN);   // ALS runs continuously from here
  tslOk = true;
  return true;
}

static uint16_t tslFullScale() { return tslIntegIdx == 0 ? 36863 : 65535; }

// Auto-range: less sensitivity when near full scale, more when counts are tiny.
// Returns true if settings changed (the next result is then discarded).
static bool tslAutoRange(uint16_t ch0) {
  uint32_t fs = tslFullScale();
  if (tslSat || ch0 > fs * 9 / 10) {
    if (tslGainIdx > 0)  { tslGainIdx--;  tslApplySettings(); return true; }
    if (tslIntegIdx > 0) { tslIntegIdx--; tslApplySettings(); return true; }
    return false;   // already LOW / 100 ms: genuine saturation, tslSat stays set
  }
  if (ch0 < 200) {
    if (tslIntegIdx < 5) { tslIntegIdx = 5; tslApplySettings(); return true; }
    if (tslGainIdx < 3)  { tslGainIdx++;  tslApplySettings(); return true; }
  }
  return false;
}

// Call every loop() iteration. Never blocks: at most one 1-byte status read and one
// 4-byte burst per integration period.
void tslPoll() {
  if (!tslOk) return;
  uint32_t now = millis();
  if (now - tslLastMs < (uint32_t)tslIntegMs + 20) return;
  uint8_t st = 0;
  if (!tslRead(TSL_REG_STATUS, &st, 1)) { tslErrCount++; tslOk = false; return; }
  if (!(st & 0x01)) return;                       // AVALID not yet set (first cycle after enable)
  uint8_t d[4];
  if (!tslRead(TSL_REG_C0DATAL, d, 4)) { tslErrCount++; tslOk = false; return; }
  tslLastMs = now;
  uint16_t ch0 = d[0] | (d[1] << 8);              // full spectrum, read first (one burst)
  uint16_t ch1 = d[2] | (d[3] << 8);              // infrared
  if (tslSettling) { tslSettling = false; return; }
  tslFull = ch0; tslIr = ch1;
  tslSat  = (ch0 >= tslFullScale()) || (ch1 >= tslFullScale());
  if (tslSat || ch0 == 0) tslLux = NAN;
  else {
    float cpl = ((float)tslIntegMs * TSL_GAIN_X[tslGainIdx]) / TSL_LUX_DF;
    tslLux = ((float)ch0 - (float)ch1) * (1.0f - (float)ch1 / (float)ch0) / cpl;
    if (tslLux < 0) tslLux = 0;
  }
  tslSampleCount++;
  tslAutoRange(ch0);
}
```

- [x] **Step 2: Write the bring-up sketch**

Create `wio-terminal/orbital-density/firmware/m1_tsl2591/m1_tsl2591.ino`:

```cpp
// Milestone 1 — TSL2591 optical sky sensor bring-up (serial only).
// WIRING: Grove I2C Hub on the LEFT Grove port -> Adafruit #4528 Grove-to-STEMMA QT cable -> TSL2591.
// Prints raw full/IR counts, gain, integration time and lux once per second; auto-range on.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>
#include "tsl2591.h"

uint32_t lastPrint = 0, lastProbe = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);   // MLX90614 shares this bus: SMBus, 100 kHz max
  if (!tslInit()) Serial.println("TSL2591 not found at 0x29 (ID != 0x50)");
}

void loop() {
  if (!tslOk && millis() - lastProbe >= 5000) { lastProbe = millis(); tslInit(); }
  tslPoll();
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    if (!tslOk) { Serial.println("TSL2591: not present"); return; }
    unsigned vis = tslFull >= tslIr ? tslFull - tslIr : 0;
    Serial.printf("full=%5u ir=%5u vis=%5u gain=%-4s integ=%3u ms lux=",
                  tslFull, tslIr, vis, tslGainName(), tslIntegMs);
    if (isnan(tslLux)) Serial.print(tslSat ? "SAT" : "--");
    else Serial.printf("%.4f", tslLux);
    Serial.printf("  n=%lu err=%lu\n", (unsigned long)tslSampleCount, (unsigned long)tslErrCount);
  }
}
```

- [x] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m1_tsl2591
```

Expected: clean compile.

- [x] **Step 4: Hardware checkpoint (user)**

Upload, open the serial monitor. Expected within ~5 s:
- indoor daylight: `gain=low` or `med`, lux in the tens to hundreds, `n` increasing by ~3 per second at 300 ms;
- cover the sensor with a hand: within a few seconds `gain=max integ=600 ms`, lux well below 1, `n` increasing by ~1–2 per second;
- phone torch on the sensor: `lux=SAT` for one line, then gain steps down until lux is a number again;
- `err` stays 0.

Result 2026-09-03: passed. Indoor 180 lux at med/300 ms; torch stepped gain down to low within a second; opaque cover stepped med→high at 0.69→0.65 lux (consistent across gains); dimming room stepped low/300→low/600→med/600 at the 200-count threshold. A bare hand only reached 32 lux (skin passes near-IR), so use an opaque object for dark tests. `max` gain and the SAT flag path were not reached and will be exercised at night.

- [x] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m1_tsl2591
git commit -m "orbital-density: add non-blocking TSL2591 driver and bring-up sketch"
```

---

### Task 3: `m1_mmc5603` bring-up (continuous mode, read time measured)

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m1_mmc5603/m1_mmc5603.ino`

**Interfaces:**
- Produces: proof that `Adafruit_MMC5603::getEvent()` in continuous mode returns in well under 5 ms, and plausible Earth-field values. Task 8 reuses the init sequence verbatim.

- [x] **Step 1: Write the sketch**

Create `wio-terminal/orbital-density/firmware/m1_mmc5603/m1_mmc5603.ino`:

```cpp
// Milestone 1 — MMC5603 magnetometer bring-up (serial only).
// WIRING: hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> MMC5603 (I2C 0x30), on the arm.
// (With a single #4528 cable: chain it from the TSL2591's second QT connector instead.)
// Continuous mode at 10 Hz so getEvent() is a plain register read — the Adafruit library
// busy-waits (delay(5) loop) in one-shot mode, which the main firmware cannot afford.
// Auto set/reset (CTRL0 bit 5, Auto_SR_en) is enabled with a raw register write: the datasheet
// recommends it, the raw bridge offset is specified at up to ±1 G (±100 uT) per axis without
// it, and the Adafruit library never sets it (its reset() even leaves the sensor in RESET
// polarity, output = -H + offset). Seen 2026-09-03: a steady 173 uT "field" that was offset.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>
#include <Adafruit_MMC56x3.h>

Adafruit_MMC5603 mmc;
bool magOk = false;
uint32_t lastPrint = 0, lastProbe = 0;

const uint8_t MMC_CTRL0_REG = 0x1B, MMC_CTRL0_AUTO_SR_EN = 0x20;

// Auto_SR_en is a level bit in the write-only CTRL0. The library's setContinuousMode() writes
// CTRL0 = 0x80 (Cmm_freq_en pulse), so this must run AFTER it; Cmm_freq_en self-clears, so
// writing it back as 0 here does not stop continuous mode.
bool mmcEnableAutoSetReset() {
  Wire.beginTransmission(MMC56X3_DEFAULT_ADDRESS);
  Wire.write(MMC_CTRL0_REG);
  Wire.write(MMC_CTRL0_AUTO_SR_EN);
  return Wire.endTransmission() == 0;
}

bool magInit() {
  if (!mmc.begin(MMC56X3_DEFAULT_ADDRESS, &Wire)) return false;   // checks product ID 0x10
  mmc.setDataRate(10);          // Hz
  mmc.setContinuousMode(true);
  return mmcEnableAutoSetReset();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);
  magOk = magInit();
  if (!magOk) Serial.println("MMC5603 not found at 0x30");
}

void loop() {
  if (!magOk) {
    if (millis() - lastProbe >= 5000) { lastProbe = millis(); magOk = magInit(); }
    return;
  }
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    sensors_event_t e;
    uint32_t t0 = micros();
    bool ok = mmc.getEvent(&e);
    uint32_t dt = micros() - t0;
    if (!ok) { Serial.println("MMC5603 read failed"); magOk = false; return; }
    float x = e.magnetic.x, y = e.magnetic.y, z = e.magnetic.z;
    float total = sqrtf(x * x + y * y + z * z);
    float heading = atan2f(y, x) * 180.0f / PI;
    if (heading < 0) heading += 360.0f;
    Serial.printf("x=%7.2f y=%7.2f z=%7.2f |B|=%6.2f uT  heading=%5.1f  read=%lu us\n",
                  x, y, z, total, heading, (unsigned long)dt);
  }
}
```

- [x] **Step 2: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m1_mmc5603
```

- [x] **Step 3: Hardware checkpoint (user)**

Upload, watch the serial output:
- `|B|` roughly 40–60 µT away from the terminal, phone and steel (Earth's field; a steady value near 100 µT or more means auto set/reset is not active or a magnet is close);
- `read=` under 2000 µs every line (this is the non-blocking proof; if it is ~5000+ µs, continuous mode did not engage);
- rotate the board flat through a full turn: `heading` sweeps 0–360 smoothly;
- bring a magnet or the Wio's own speaker corner near: `|B|` jumps — this is the effect the arm is for.

Result 2026-09-03: passed after enabling auto set/reset. Before it the sensor read a steady 173 µT (pure bridge offset, library leaves RESET polarity); after it 47–58 µT, reads 1.17 ms, heading covered all twelve 30° sectors during a hand rotation. Rough hard-iron estimate from that fast turn: x ≈ +23 µT, y ≈ +1 µT; Task 14 refines it.

- [x] **Step 4: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m1_mmc5603
git commit -m "orbital-density: add MMC5603 magnetometer bring-up sketch"
```

---

### Task 4 (DEFERRED): `m1_mlx90614` bring-up (GY-906, read time measured)

> **Deferred 2026-09-03:** the GY-906 header is not soldered yet. Skip this task and Task 9 for now; run both, in order, when the module is ready. Wire it into the spare chassis Grove I²C socket (or a hub socket) with the Grove-to-female-jumper cable.

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m1_mlx90614/m1_mlx90614.ino`

**Interfaces:**
- Produces: proof of a working SMBus read on the shared 100 kHz bus and the ambient/object/delta convention used by Task 9.

- [ ] **Step 0: Install the library**

```bash
arduino-cli lib install "Adafruit MLX90614 Library"
```

- [ ] **Step 1: Write the sketch**

Create `wio-terminal/orbital-density/firmware/m1_mlx90614/m1_mlx90614.ino`:

```cpp
// Milestone 1 — MLX90614ESF-BCC (GY-906 module) thermal sky sensor bring-up (serial only).
// WIRING: spare chassis Grove I2C socket (or a hub socket) -> Grove-to-female-jumper cable -> GY-906:
//   Grove yellow (SCL) -> SCL, white (SDA) -> SDA, red -> VIN (3.3 V), black -> GND.
// SMBus device: the whole Wire bus stays at 100 kHz. -BCC = 3 V, 35° FOV, gradient compensated.
// "object" is long-wave IR sky brightness temperature, not the temperature of space.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>
#include <Adafruit_MLX90614.h>

Adafruit_MLX90614 mlx;
bool mlxOk = false;
uint32_t lastPrint = 0, lastProbe = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);
  mlxOk = mlx.begin(MLX90614_I2CADDR, &Wire);
  if (!mlxOk) Serial.println("MLX90614 not found at 0x5A");
}

void loop() {
  if (!mlxOk) {
    if (millis() - lastProbe >= 5000) { lastProbe = millis(); mlxOk = mlx.begin(MLX90614_I2CADDR, &Wire); }
    return;
  }
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    uint32_t t0 = micros();
    double amb = mlx.readAmbientTempC();
    double obj = mlx.readObjectTempC();
    uint32_t dt = micros() - t0;
    if (isnan(amb) || isnan(obj)) { Serial.println("MLX90614 read failed (NAN)"); return; }
    Serial.printf("ambient=%6.2f C  object(sky IR)=%6.2f C  delta=%6.2f C  read=%lu us\n",
                  amb, obj, amb - obj, (unsigned long)dt);
  }
}
```

- [ ] **Step 2: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m1_mlx90614
```

- [ ] **Step 3: Hardware checkpoint (user)**

Upload, watch the output:
- `ambient` near room temperature; `object` near room temperature when pointed at a wall, ~30–34 °C at a palm held 5 cm away;
- pointed out a window at open sky: `object` clearly below `ambient` (tens of degrees on a clear night, a few degrees under thick cloud);
- `read=` under 3000 µs for the two reads together;
- no `NAN` lines. If every line is NAN, SCL/SDA are swapped or the bus clock was left above 100 kHz.

- [ ] **Step 4: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m1_mlx90614
git commit -m "orbital-density: add MLX90614 thermal sky bring-up sketch"
```

---

### Task 5: Loop-timing instrumentation and the NMEA baseline in `m2_skyview`

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/loopstats.h`
- Modify: `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino` (includes at 19–24; `loop()` at 1220–1258)
- Modify: `wio-terminal/orbital-density/README.md` (new subsection under "Firmware")

**Interfaces:**
- Produces: `void loopStatsTick();` (every iteration), `void loopStatsRoll();` (once per second), `uint32_t loopMaxMsLast, loopIterLast;` used by Task 10 (`Observation.loopMaxMs`) and Task 12 (Sensors page). Regression metric: `gps.failedChecksum()` (TinyGPS++ built-in).

- [x] **Step 1: Write `loopstats.h`**

```cpp
// loopstats.h — loop-timing counters: the regression metric for every sensor added to loop().
// A blocking call shows up as loopMaxMsLast well above ~22 ms AND a rising
// gps.failedChecksum() rate, because the 256-byte UART RX buffer overflows at 115200 baud.
#pragma once
#include <Arduino.h>

uint32_t loopMaxMsLast = 0;    // longest iteration in the previous 1 s window, ms (rounded up)
uint32_t loopIterLast  = 0;    // iterations in the previous 1 s window
static uint32_t loopMaxUs = 0, loopIter = 0, loopPrevUs = 0;

// Call first thing in every loop() iteration.
inline void loopStatsTick() {
  uint32_t now = micros();
  if (loopPrevUs) { uint32_t dt = now - loopPrevUs; if (dt > loopMaxUs) loopMaxUs = dt; }
  loopPrevUs = now;
  loopIter++;
}

// Call once per second (from the existing 1 Hz block) to close the window.
inline void loopStatsRoll() {
  loopMaxMsLast = (loopMaxUs + 999) / 1000;
  loopIterLast  = loopIter;
  loopMaxUs = 0; loopIter = 0;
}
```

- [x] **Step 2: Include it and hook the loop**

In `m2_skyview.ino`, after line 24 (`#include <SparkFunBQ27441.h>   // fuel gauge ...`) add:

```cpp
#include "loopstats.h"         // loop max-time + iteration counters (see Sensors page / status line)
```

In `loop()`: make `loopStatsTick();` the first statement (before `feedGps();`). In the 1 Hz block (`if (millis() - lastPrint >= 1000) {`), insert `loopStatsRoll();` immediately after `lastPrint = millis();`, and replace the status `Serial.printf(...)` with:

```cpp
    // Status line. NOTE: this core's Serial.printf() truncates at ~80 chars, so the line is
    // built from several printf calls; each sensor module appends its own segment.
    Serial.printf("inView=%d pos=%d used=%d %s | GPS=%d GLO=%d BDS=%d QZS=%d",
      countInView(), countPositioned(), gps.satellites.isValid()?(int)gps.satellites.value():-1, fixStr(),
      countConstel(C_GPS), countConstel(C_GLO), countConstel(C_BDS), countConstel(C_QZS));
    Serial.printf(" | T=%.1f H=%.0f P=%.0f", bmeTemp, bmeHum, bmePres);
    Serial.printf(" | loopMax=%lums iter=%lu nmeaPass=%lu nmeaFail=%lu",
      (unsigned long)loopMaxMsLast, (unsigned long)loopIterLast,
      (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum());
    Serial.println();
```

(The single long `printf` was tried first and lost everything after ~80 characters.)

- [x] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [x] **Step 4: Hardware checkpoint (user) — record the baseline**

Upload with the hub connected (sensors plugged in but not yet used by the firmware), GPS with a fix. Let it run 5 minutes with the screen on, then read the last status line and one from 5 minutes earlier. Ask the user for: typical `loopMax` (screen on), `nmeaPass` and `nmeaFail` at both times.

Add to `README.md`, under the "Firmware" heading, a subsection:

```markdown
### Loop timing baseline (commit <hash>, hub attached, no new drivers)

| metric | value |
|---|---|
| loopMax, screen on (typical) | NN ms |
| loopMax, screen off (typical) | NN ms |
| NMEA sentences failed / 5 min | NN (of NNNN passed) |

Every sensor task below is accepted only if the failed-checksum rate stays within this baseline.
The 1 Hz `drawPage()` sprite blit is already the largest blocking step; anything that adds
tens of ms per second will show here first. Fallback if needed: build with
`--build-property "compiler.cpp.extra_flags=-DSERIAL_BUFFER_SIZE=1024"`.
```

Fill in the numbers the user reports.

Result 2026-09-03 (5 min, screen on, 3D fix): loopMax median 138 ms / max 151 ms; ~234k iterations/s; NMEA pass 7.4/s; **fail 111 per 5 min (0.37/s, ~5 %)** — the pre-existing sprite-blit/ephemeris stall already costs sentences. Regression gate for later tasks: fail rate ≤ ~0.4/s.

- [x] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/loopstats.h wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino wio-terminal/orbital-density/README.md
git commit -m "orbital-density: add loop timing counters and record NMEA baseline"
```

---

### Task 6: `i2c_bus.h` — bus clock, presence probe, boot scan

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/i2c_bus.h`
- Modify: `m2_skyview.ino` includes; `setup()` around line 1195 (`Wire.begin();`)

**Interfaces:**
- Produces: `const uint32_t I2C_CLOCK_HZ, I2C_REPROBE_MS;` `bool i2cPresent(uint8_t);` `void i2cScan();` `uint8_t i2cFound[16], i2cFoundCount;` used by Tasks 7–9 (re-probe cadence) and Task 12 (Sensors page).

- [x] **Step 1: Write `i2c_bus.h`**

```cpp
// i2c_bus.h — shared helpers for the sensor hub on the LEFT Grove port (Wire = SERCOM3, PA16/PA17).
// Bus members: 0x29 TSL2591, 0x30 MMC5603, 0x55 BQ27441 (battery chassis), 0x5A MLX90614, 0x77 BME280.
// 100 kHz is a hard limit: the MLX90614 is an SMBus device. Do not raise it for the others.
#pragma once
#include <Arduino.h>
#include <Wire.h>

const uint32_t I2C_CLOCK_HZ   = 100000;
const uint32_t I2C_REPROBE_MS = 30000;   // retry xxxInit() for missing sensors this often (hot-plug)

uint8_t i2cFound[16];
uint8_t i2cFoundCount = 0;

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Scan 0x08..0x77 once (≈25 ms at 100 kHz — setup() only, never from loop()).
void i2cScan() {
  i2cFoundCount = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++)
    if (i2cPresent(a) && i2cFoundCount < sizeof(i2cFound)) i2cFound[i2cFoundCount++] = a;
  Serial.print("I2C scan:");
  for (uint8_t i = 0; i < i2cFoundCount; i++) Serial.printf(" 0x%02X", i2cFound[i]);
  Serial.printf(" (%u devices)\n", i2cFoundCount);
}
```

- [x] **Step 2: Wire it into the sketch**

Add after the `loopstats.h` include:

```cpp
#include "i2c_bus.h"           // 100 kHz clock constant, presence probe, boot scan
```

In `setup()`, replace

```cpp
  Wire.begin();                 // fuel gauge is optional (battery chassis only)
```

with

```cpp
  Wire.begin();                 // fuel gauge is optional (battery chassis only)
  Wire.setClock(I2C_CLOCK_HZ);  // MLX90614 is SMBus: 100 kHz max for the whole hub
  i2cScan();                    // echo what answered; shown on the Sensors page
```

- [x] **Step 3: Compile, upload, confirm**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

Hardware checkpoint (user): the first serial line after reset reads `I2C scan: 0x29 0x30 0x55 0x77 (4 devices)` (`0x5A` joins once the GY-906 is in). In practice the boot line is not observable over native USB (the host has not reopened the port yet), so the Sensors page (Task 12) is where the scan result is checked; the BME280 page still showing readings is the interim check. Result 2026-09-03: compiled, BME280 22.0 °C / 54 % / 1005 hPa on the hub.

- [ ] **Step 4: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/i2c_bus.h wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "orbital-density: pin I2C to 100 kHz and scan the hub at boot"
```

---

### Task 7: TSL2591 in the main firmware

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/tsl2591.h` (copy of Task 2's file)
- Modify: `m2_skyview.ino` includes; `setup()` (after `bmeOk = bmeInit();`), `loop()` (after `pollDust();` and a new re-probe block), status line

**Interfaces:**
- Consumes: `tslInit/tslPoll/tslOk/...` from Task 2; `I2C_REPROBE_MS` from Task 6.
- Produces: `uint32_t lastReprobeMs;` and the re-probe block that Tasks 8–9 extend.

- [x] **Step 1: Copy the driver**

```bash
cp wio-terminal/orbital-density/firmware/m1_tsl2591/tsl2591.h wio-terminal/orbital-density/firmware/m2_skyview/tsl2591.h
```

The `m1_tsl2591` copy stays as the frozen bring-up snapshot (same convention as `m1_bme280`).

- [x] **Step 2: Include, init, poll, re-probe**

Add after the `i2c_bus.h` include:

```cpp
#include "tsl2591.h"           // optical sky (0x29), register-level non-blocking driver
```

In `setup()`, after `if (bmeOk) bmeRead();` add:

```cpp
  tslInit();                    // optical sky sensor (0x29); re-probed every 30 s if absent
```

Just above `void loop() {` add:

```cpp
uint32_t lastReprobeMs = 0;   // missing-sensor re-init cadence (hot-plug, flaky cable)
```

In `loop()`, right after `pollDust();` add:

```cpp
  tslPoll();

  if (millis() - lastReprobeMs >= I2C_REPROBE_MS) {
    lastReprobeMs = millis();
    if (!tslOk) tslInit();
  }
```

Add a status-line segment before the `Serial.println();` in the 1 Hz block: `Serial.printf(" | tsl full=%u ir=%u %s/%ums lux=%.3f", tslFull, tslIr, tslGainName(), tslIntegMs, tslLux);`.

- [x] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [x] **Step 4: Hardware checkpoint (user)**

Upload, 5 minutes with a fix. Expected: `tsl` values change with light like in Task 2; `loopMax` unchanged versus the Task 5 baseline; `nmeaFail` growth within baseline. Unplug the TSL2591 QT cable: `tsl` freezes, no crash; plug back in: values resume within 30 s.

Result 2026-09-03: passed. 184 s run, 85 s with a 3D fix: loopMax median 130 ms (baseline 138), ~213k iterations/s, NMEA pass 10.3/s and **0 failures** while fixed (baseline 7.4/s, 0.37/s). TSL2591 reported 470–555 lux at med/300 ms. Hot-plug test deferred to the Task 8 run.

- [x] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview
git commit -m "orbital-density: integrate TSL2591 optical sky sensor"
```

---

### Task 8: MMC5603 module and `ring.h`

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/ring.h`
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/mmc5603.h`
- Modify: `m2_skyview.ino` includes, `setup()`, `loop()` (poll, re-probe, 5-min history block, 1 Hz block, status line)

**Interfaces:**
- Produces: `template<typename T,int N> struct Ring { T v[N]; int len; void push(T); }`, `const int SENSOR_HIST_N = 288;`
- Produces: `bool magOk; bool magInit(); void magPoll(); void magRollSecond(); void magPushHist();` `float magMeanX, magMeanY, magMeanZ, magMeanTotal, magMeanHeading;` `uint32_t magLastMs, magSampleCount, magErrCount;` `Ring<int16_t,288> magTotalHist;` constants `MAG_OFF_X/Y/Z, MAG_MOUNT_OFFSET_DEG, MAG_DECLINATION_DEG`.

- [x] **Step 1: Write `ring.h`**

```cpp
// ring.h — fixed-capacity history (oldest first, newest at v[len-1]) for the new sensor
// pages. 288 samples at one per 5 min = 24 h, the same window as the existing charts.
#pragma once
#include <string.h>

template <typename T, int N>
struct Ring {
  T   v[N];
  int len = 0;
  void push(T x) {
    if (len < N) { v[len++] = x; return; }
    memmove(v, v + 1, sizeof(T) * (N - 1));
    v[N - 1] = x;
  }
};

const int SENSOR_HIST_N = 288;
```

- [x] **Step 2: Write `mmc5603.h`**

```cpp
// mmc5603.h — Adafruit MMC5603 magnetometer (I2C 0x30) at the end of the 15–30 cm
// non-magnetic arm. Continuous mode at 10 Hz so getEvent() is a plain register read (the
// library busy-waits in one-shot mode). Auto set/reset (CTRL0 Auto_SR_en) is enabled by a
// raw register write: without it the raw bridge offset is up to ±1 G (±100 uT) per axis and
// the library leaves the sensor in RESET polarity (output = -H + offset) — bring-up showed a
// steady 173 uT that was pure offset. Heading assumes the instrument is level; tilt
// compensation is future work (the Wio's built-in LIS3DHTR on Wire1 is the obvious source).
#pragma once
#include <Arduino.h>
#include <Adafruit_MMC56x3.h>
#include "ring.h"

const uint16_t MAG_RATE_HZ     = 10;
const uint32_t MAG_POLL_MS     = 100;
const uint8_t  MMC_CTRL0_REG = 0x1B, MMC_CTRL0_AUTO_SR_EN = 0x20;
// Calibration constants — set from field data (README "Magnetometer calibration", tools/mag_calib.py).
const float MAG_OFF_X = 0.0f, MAG_OFF_Y = 0.0f, MAG_OFF_Z = 0.0f;   // hard-iron offsets, uT
const float MAG_MOUNT_OFFSET_DEG = 0.0f;   // board +X axis relative to the instrument's forward direction
const float MAG_DECLINATION_DEG  = 0.0f;   // magnetic -> true north for the observing site

Adafruit_MMC5603 mmc;
bool     magOk = false;
uint32_t magLastMs = 0;
float    magX = 0, magY = 0, magZ = 0, magTotal = 0, magHeading = 0;          // latest 10 Hz sample
float    magMeanX = 0, magMeanY = 0, magMeanZ = 0, magMeanTotal = 0, magMeanHeading = 0;   // last 1 s mean
static float magAccX = 0, magAccY = 0, magAccZ = 0;
static int   magAccN = 0;
uint32_t magSampleCount = 0, magErrCount = 0;
Ring<int16_t, SENSOR_HIST_N> magTotalHist;   // |B| * 10, one sample / 5 min

static float magHeadingFrom(float x, float y) {
  float h = atan2f(y, x) * 180.0f / PI + MAG_MOUNT_OFFSET_DEG + MAG_DECLINATION_DEG;
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  return h;
}

// Auto_SR_en is a level bit in the write-only CTRL0; setContinuousMode() writes CTRL0 = 0x80
// (Cmm_freq_en pulse, self-clearing), so this must come after it.
static bool mmcEnableAutoSetReset() {
  Wire.beginTransmission(MMC56X3_DEFAULT_ADDRESS);
  Wire.write(MMC_CTRL0_REG);
  Wire.write(MMC_CTRL0_AUTO_SR_EN);
  return Wire.endTransmission() == 0;
}

// Probe (product ID 0x10) + configure. Safe to call again; returns magOk.
bool magInit() {
  if (!mmc.begin(MMC56X3_DEFAULT_ADDRESS, &Wire)) { magOk = false; return false; }
  mmc.setDataRate(MAG_RATE_HZ);
  mmc.setContinuousMode(true);
  magOk = mmcEnableAutoSetReset();
  return magOk;
}

// Call every loop() iteration; touches the bus at most once per MAG_POLL_MS.
void magPoll() {
  if (!magOk) return;
  uint32_t now = millis();
  if (now - magLastMs < MAG_POLL_MS) return;
  magLastMs = now;
  sensors_event_t e;
  if (!mmc.getEvent(&e)) { magErrCount++; magOk = false; return; }
  magX = e.magnetic.x - MAG_OFF_X;
  magY = e.magnetic.y - MAG_OFF_Y;
  magZ = e.magnetic.z - MAG_OFF_Z;
  magTotal   = sqrtf(magX * magX + magY * magY + magZ * magZ);
  magHeading = magHeadingFrom(magX, magY);
  magAccX += magX; magAccY += magY; magAccZ += magZ; magAccN++;
  magSampleCount++;
}

// Call once per second, before the observation is assembled: folds the last second's
// samples into the means and restarts the accumulator.
void magRollSecond() {
  if (magAccN == 0) return;
  magMeanX = magAccX / magAccN; magMeanY = magAccY / magAccN; magMeanZ = magAccZ / magAccN;
  magMeanTotal   = sqrtf(magMeanX * magMeanX + magMeanY * magMeanY + magMeanZ * magMeanZ);
  magMeanHeading = magHeadingFrom(magMeanX, magMeanY);
  magAccX = magAccY = magAccZ = 0; magAccN = 0;
}

// Call every 5 min (existing HIST block).
void magPushHist() { if (magOk) magTotalHist.push((int16_t)(magMeanTotal * 10.0f + 0.5f)); }
```

- [x] **Step 3: Wire it into the sketch**

Add after the `tsl2591.h` include:

```cpp
#include "mmc5603.h"           // magnetometer (0x30) on the arm; continuous 10 Hz
```

`setup()`, after `tslInit();`:

```cpp
  magInit();                    // magnetometer (0x30); re-probed every 30 s if absent
```

`loop()`: after `tslPoll();` add `magPoll();`. In the re-probe block add `if (!magOk) magInit();`. In the 5-min `HIST_PERIOD_MS` block, after `pushConstelHist();` add `magPushHist();`. In the 1 Hz block, after `if (bmeOk) bmeRead();` add `magRollSecond();`. Add a status-line segment before the `Serial.println();`: `Serial.printf(" | mag |B|=%.1f hdg=%.0f", magMeanTotal, magMeanHeading);`.

- [x] **Step 4: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [x] **Step 5: Hardware checkpoint (user)**

Upload, 5 minutes. Expected: `|B|` 40–60 µT and `hdg` steady when still, away from the terminal; `loopMax` and `nmeaFail` within baseline. Unplug/replug the MMC5603: values freeze then resume within 30 s.

Result 2026-09-03: passed. 184 s with a 3D fix: loopMax median 137 ms (baseline 138), ~195k iterations/s, NMEA pass 8.0/s, fail 0.26/s (baseline 0.37). |B| 50.6–56.7 µT, heading steady. Hot-plug test done on the TSL2591 instead: frozen 73 s (≈40 s unplugged + re-probe), then resumed; MMC5603 and BME280 unaffected.

- [x] **Step 6: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview
git commit -m "orbital-density: integrate MMC5603 magnetometer with 1 s means and 24 h history"
```

---

### Task 9 (DEFERRED): MLX90614 module behind `MLX_ENABLED`

> **Deferred 2026-09-03:** run after Task 4, once the GY-906 is soldered. Tasks 10 and 12 already contain their `#if MLX_ENABLED` sections, so this task is the driver header plus the guarded hooks and the flag flip.

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/mlx90614.h`
- Modify: `m2_skyview.ino` includes, `setup()`, `loop()` (poll, re-probe, 5-min block, status line)

**Interfaces:**
- Consumes: `Ring`, `SENSOR_HIST_N` (Task 8).
- Produces: `bool mlxOk; bool mlxInit(); void mlxPoll(); void mlxPushHist();` `float mlxAmbC, mlxObjC, mlxDeltaC;` `uint32_t mlxLastMs, mlxSampleCount, mlxErrCount;` `Ring<int16_t,288> mlxDeltaHist;`

- [ ] **Step 1: Write `mlx90614.h`**

```cpp
// mlx90614.h — MLX90614ESF-BCC (GY-906 module, I2C/SMBus 0x5A) pointed at the zenith:
// long-wave IR "sky brightness temperature". The shared Wire bus stays at 100 kHz for it.
// Two word reads per second (<1 ms each). The object value is NOT the temperature of
// space — label it "Sky IR" everywhere. delta = ambient - object (large = clear sky).
#pragma once
#include <Arduino.h>
#include <Adafruit_MLX90614.h>
#include "ring.h"

const uint32_t MLX_POLL_MS = 1000;

Adafruit_MLX90614 mlx;
bool     mlxOk = false;
uint32_t mlxLastMs = 0;
float    mlxAmbC = NAN, mlxObjC = NAN, mlxDeltaC = NAN;
uint32_t mlxSampleCount = 0, mlxErrCount = 0;
static uint8_t mlxConsecErr = 0;
Ring<int16_t, SENSOR_HIST_N> mlxDeltaHist;   // delta * 10, one sample / 5 min

// Probe (ACK) + plausibility read. Safe to call again; returns mlxOk.
bool mlxInit() {
  if (!mlx.begin(MLX90614_I2CADDR, &Wire)) { mlxOk = false; return false; }
  double a = mlx.readAmbientTempC();
  if (isnan(a) || a < -40.0 || a > 85.0) { mlxOk = false; return false; }
  mlxConsecErr = 0;
  mlxOk = true;
  return true;
}

// Call every loop() iteration; talks to the sensor once per MLX_POLL_MS.
void mlxPoll() {
  if (!mlxOk) return;
  uint32_t now = millis();
  if (now - mlxLastMs < MLX_POLL_MS) return;
  mlxLastMs = now;
  double a = mlx.readAmbientTempC();
  double o = mlx.readObjectTempC();
  if (isnan(a) || isnan(o)) {
    mlxErrCount++;
    if (++mlxConsecErr >= 5) mlxOk = false;   // five bad seconds in a row: treat as unplugged
    return;
  }
  mlxConsecErr = 0;
  mlxAmbC = a; mlxObjC = o; mlxDeltaC = a - o;
  mlxSampleCount++;
}

// Call every 5 min (existing HIST block).
void mlxPushHist() { if (mlxOk && !isnan(mlxDeltaC)) mlxDeltaHist.push((int16_t)(mlxDeltaC * 10.0f)); }
```

- [ ] **Step 2: Wire it into the sketch**

Change `#define MLX_ENABLED 0` (Task 10) to `1`. Add after the `mmc5603.h` include:

```cpp
#if MLX_ENABLED
#include "mlx90614.h"          // thermal IR sky (0x5A), SMBus — keeps the bus at 100 kHz
#endif
```

Each of the following goes inside its own `#if MLX_ENABLED` … `#endif`: in `setup()`, after `magInit();`, `mlxInit();`; in `loop()`, after `magPoll();`, `mlxPoll();`; in the re-probe block `if (!mlxOk) mlxInit();`; in the 5-min block after `magPushHist();`, `mlxPushHist();`. Leave the serial status line alone; the Sensors page and `/obs.csv` carry the values.

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [ ] **Step 4: Hardware checkpoint (user)**

Upload, 5 minutes, **all sensors active**. Expected: sensible `mlx` values on the SkySens page; `loopMax` and `nmeaFail` within the Task 5 baseline. This is the gate for the "drivers never block" requirement: if `nmeaFail` grows faster than baseline, find which `xxxPoll()` is responsible by commenting them out one at a time before proceeding.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview
git commit -m "orbital-density: integrate MLX90614 thermal sky sensor"
```

---

### Task 10: `observation.h` and the 1 Hz `/obs.csv` log

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/observation.h`
- Modify: `m2_skyview.ino` includes; new block before `void initSd()` (line 1119); 1 Hz block in `loop()`

**Interfaces:**
- Consumes: every `xxxOk`/value global from Tasks 7–8 (Task 9 only under `MLX_ENABLED`), `bmeOk/bmeTemp/bmeHum/bmePres`, `loopMaxMsLast`, `gps`, `countInView()`, `sdOk`, `SD`.
- Produces: `struct Observation; Observation obs;` `bool obsHeader(char*, size_t); bool obsRow(const Observation&, char*, size_t);` sketch-side `void assembleObservation(); void logObs();` `uint32_t obsRowsWritten, obsWriteErrors; bool obsOpen;` `const int OBS_EVERY_N;`

- [x] **Step 1: Write `observation.h`**

```cpp
// observation.h — the unified per-second record: GNSS time/position plus every sensor's
// latest values. Assembled once per second (assembleObservation() in the sketch) and
// written to /obs.csv. Header AND rows come from OBS_COLS below, so they cannot drift apart
// (/gps.csv keeps two hand-maintained literals; do not copy that pattern here).
#pragma once
#include <Arduino.h>

struct Observation {
  char     utc[24];          // "YYYY-MM-DDTHH:MM:SSZ", "" until the first GNSS date+time
  uint32_t uptimeS;
  bool     fixValid; double lat, lon; float altM, hdop; uint8_t used, inView; const char* fix;
  bool     tslOk; uint16_t tslFull, tslIr; const char* tslGain; uint16_t tslIntegMs; float tslLux; bool tslSat;
  bool     mlxOk; float mlxAmbC, mlxObjC, mlxDeltaC;
  bool     magOk; float bx, by, bz, bTotal, headingDeg;
  bool     bmeOk; float tempC, hum, presHpa;
  uint16_t loopMaxMs;
};

Observation obs;   // current record

typedef void (*ObsFmt)(char* out, size_t n, const Observation& o);
struct ObsCol { const char* name; ObsFmt fmt; };

// Absent sensor or invalid value -> empty field, never a fake zero.
static void fmtF(char* b, size_t n, bool ok, float v, const char* f) { if (ok && !isnan(v)) snprintf(b, n, f, v); else b[0] = 0; }
static void fmtU(char* b, size_t n, bool ok, unsigned v)             { if (ok) snprintf(b, n, "%u", v); else b[0] = 0; }

const ObsCol OBS_COLS[] = {
  {"utc",           [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%s", o.utc); }},
  {"uptime_s",      [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%lu", (unsigned long)o.uptimeS); }},
  {"lat",           [](char* b, size_t n, const Observation& o){ if (o.fixValid) snprintf(b, n, "%.6f", o.lat); else b[0] = 0; }},
  {"lon",           [](char* b, size_t n, const Observation& o){ if (o.fixValid) snprintf(b, n, "%.6f", o.lon); else b[0] = 0; }},
  {"alt_m",         [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.fixValid, o.altM, "%.1f"); }},
  {"fix",           [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%s", o.fix); }},
  {"hdop",          [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.fixValid, o.hdop, "%.1f"); }},
  {"used",          [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.used); }},
  {"in_view",       [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.inView); }},
  {"tsl_full",      [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslFull); }},
  {"tsl_ir",        [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslIr); }},
  {"tsl_vis",       [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslFull >= o.tslIr ? o.tslFull - o.tslIr : 0); }},
  {"tsl_gain",      [](char* b, size_t n, const Observation& o){ if (o.tslOk) snprintf(b, n, "%s", o.tslGain); else b[0] = 0; }},
  {"tsl_integ_ms",  [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslIntegMs); }},
  {"tsl_lux",       [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.tslOk, o.tslLux, "%.4f"); }},
  {"tsl_sat",       [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslSat ? 1 : 0); }},
  {"mlx_ambient_c", [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxAmbC, "%.2f"); }},
  {"mlx_object_c",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxObjC, "%.2f"); }},
  {"mlx_delta_c",   [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxDeltaC, "%.2f"); }},
  {"mag_x_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bx, "%.2f"); }},
  {"mag_y_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.by, "%.2f"); }},
  {"mag_z_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bz, "%.2f"); }},
  {"mag_total_ut",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bTotal, "%.2f"); }},
  {"heading_deg",   [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.headingDeg, "%.1f"); }},
  {"temp_c",        [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.tempC, "%.2f"); }},
  {"humidity",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.hum, "%.1f"); }},
  {"pressure_hpa",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.presHpa, "%.1f"); }},
  {"sensors_ok",    [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", (o.tslOk ? 1 : 0) | (o.mlxOk ? 2 : 0) | (o.magOk ? 4 : 0) | (o.bmeOk ? 8 : 0)); }},
  {"loop_max_ms",   [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.loopMaxMs); }},
};
const int OBS_COL_COUNT = sizeof(OBS_COLS) / sizeof(OBS_COLS[0]);

// "name,name,...\n" -> out. False if it did not fit.
bool obsHeader(char* out, size_t n) {
  size_t pos = 0;
  for (int i = 0; i < OBS_COL_COUNT; i++) {
    int w = snprintf(out + pos, n - pos, "%s%s", i ? "," : "", OBS_COLS[i].name);
    if (w < 0 || (size_t)w >= n - pos) return false;
    pos += w;
  }
  return snprintf(out + pos, n - pos, "\n") < (int)(n - pos);
}

// One CSV row for o -> out. False if it did not fit.
bool obsRow(const Observation& o, char* out, size_t n) {
  size_t pos = 0;
  char f[32];
  for (int i = 0; i < OBS_COL_COUNT; i++) {
    OBS_COLS[i].fmt(f, sizeof(f), o);
    int w = snprintf(out + pos, n - pos, "%s%s", i ? "," : "", f);
    if (w < 0 || (size_t)w >= n - pos) return false;
    pos += w;
  }
  return snprintf(out + pos, n - pos, "\n") < (int)(n - pos);
}
```

- [x] **Step 2: Add the sketch-side glue**

Add after the `mmc5603.h` include:

```cpp
// MLX90614 / GY-906 is deferred until its header is soldered. 0 = driver not compiled, the
// mlx_* CSV columns stay in the schema (written empty), pages say "not installed".
// Set to 1 after running the deferred Tasks 4 and 9.
#define MLX_ENABLED 0
#include "observation.h"       // unified 1 Hz record + /obs.csv column table
```

Insert this block in `m2_skyview.ino` immediately **before** `void initSd() {`:

```cpp
// ---------- observation log (/obs.csv, one row per second) ----------
// Separate from /gps.csv (60 s, legacy schema). File stays open; flushed every 10 s so a
// 1 Hz cadence costs one buffered print per second, not an open/close on the card.
// OBS_EVERY_N = 1 logs every second; raise it (e.g. 10) to log every N seconds.
const char*    OBS_PATH     = "/obs.csv";
const int      OBS_EVERY_N  = 1;
const uint32_t OBS_FLUSH_MS = 10000;
File     obsFile;
bool     obsOpen = false;
uint32_t lastObsFlushMs = 0;
uint32_t obsRowsWritten = 0, obsWriteErrors = 0;

void assembleObservation() {
  obs.uptimeS = millis() / 1000;
  if (gps.date.isValid() && gps.time.isValid())
    snprintf(obs.utc, sizeof(obs.utc), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  else obs.utc[0] = 0;
  obs.fixValid = gps.location.isValid();
  obs.lat  = obs.fixValid ? gps.location.lat() : 0.0;
  obs.lon  = obs.fixValid ? gps.location.lng() : 0.0;
  obs.altM = gps.altitude.isValid() ? (float)gps.altitude.meters() : NAN;
  obs.hdop = gps.hdop.isValid() ? (float)gps.hdop.hdop() : NAN;
  obs.used = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
  obs.inView = (uint8_t)countInView();
  obs.fix  = gps.location.isValid() ? (gps.altitude.isValid() ? "3D" : "2D") : "none";
  obs.tslOk = tslOk; obs.tslFull = tslFull; obs.tslIr = tslIr; obs.tslGain = tslGainName();
  obs.tslIntegMs = tslIntegMs; obs.tslLux = tslLux; obs.tslSat = tslSat;
#if MLX_ENABLED
  obs.mlxOk = mlxOk; obs.mlxAmbC = mlxAmbC; obs.mlxObjC = mlxObjC; obs.mlxDeltaC = mlxDeltaC;
#else
  obs.mlxOk = false; obs.mlxAmbC = obs.mlxObjC = obs.mlxDeltaC = NAN;   // GY-906 deferred: columns stay, fields empty
#endif
  obs.magOk = magOk; obs.bx = magMeanX; obs.by = magMeanY; obs.bz = magMeanZ;
  obs.bTotal = magMeanTotal; obs.headingDeg = magMeanHeading;
  obs.bmeOk = bmeOk; obs.tempC = bmeTemp; obs.hum = bmeHum; obs.presHpa = bmePres;
  obs.loopMaxMs = (uint16_t)loopMaxMsLast;
}

bool obsOpenFile() {
  if (!sdOk) return false;
  bool isNew = !SD.exists(OBS_PATH);
  obsFile = SD.open(OBS_PATH, FILE_APPEND);
  if (!obsFile) return false;
  if (isNew) { char h[512]; if (obsHeader(h, sizeof(h))) obsFile.print(h); }
  lastObsFlushMs = millis();
  obsOpen = true;
  return true;
}

void logObs() {
  if (!sdOk) { if (obsOpen) { obsFile.close(); obsOpen = false; } return; }   // logRow() re-inits the card
  if (!obsOpen && !obsOpenFile()) return;
  char row[512];
  if (!obsRow(obs, row, sizeof(row))) return;
  if (obsFile.print(row) == 0) { obsWriteErrors++; obsFile.close(); obsOpen = false; return; }
  obsRowsWritten++;
  if (millis() - lastObsFlushMs >= OBS_FLUSH_MS) { lastObsFlushMs = millis(); obsFile.flush(); }
}
```

In the 1 Hz block of `loop()`, after `magRollSecond();` add:

```cpp
    assembleObservation();
    static uint8_t obsTick = 0;
    if (++obsTick >= OBS_EVERY_N) { obsTick = 0; logObs(); }
```

(Keep `if (screenOn) drawPage();` after these lines.) Add a status-line segment before the `Serial.println();`: `Serial.printf(" | obs rows=%lu err=%lu", (unsigned long)obsRowsWritten, (unsigned long)obsWriteErrors);`.

- [x] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

If the compiler rejects the lambda-to-function-pointer conversions in `OBS_COLS`, replace each lambda with a named `static void fmtXxx(char*, size_t, const Observation&)` function above the table; the table content is unchanged.

- [x] **Step 4: Hardware checkpoint (user)**

Upload with the SD card in, run 3 minutes, power off, read the card on a PC:
- `/obs.csv` exists, first line is the 29-name header, then ~1 row per second;
- `utc` blank before the first fix, ISO-8601 after; the three `mlx_*` fields empty on every row (sensor deferred);
- unplug the TSL2591 for a minute during the run: `tsl_*` fields empty (not `0`) for those rows, `sensors_ok` drops by 1;
- `loop_max_ms` column: the periodic `flush()` should show as an occasional bump every ~10 s; `nmeaFail` growth still within baseline over 5 minutes. If not, raise `OBS_FLUSH_MS` to 30000 first, then consider `OBS_EVERY_N`.

Quick sanity check on the PC:

```bash
head -3 obs.csv; awk -F, 'NR>1{n++} END{print n" rows"}' obs.csv
```

Result 2026-09-03: 5 min with a 3D fix: 299 rows in 300 s, 0 write errors, loopMax median 139 ms / max 156 ms (10 s flush), NMEA pass 12.5/s, fail 0.00/s. Card contents (header, empty `mlx_*` fields) to be checked at the Task 12 card pull.

- [x] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview
git commit -m "orbital-density: add unified Observation record and 1 Hz /obs.csv log"
```

---

### Task 11: Page table, 5-way navigation, and the dust gate

**Files:**
- Modify: `m2_skyview.ino` — `#define DUST_ENABLED` near the includes; `pollButtons()` (782–790); `drawPage()` (1153–1166); `setup()` pin modes and dust init (1190–1212); `loop()` dust calls (1222, 1235); `logRow()` dust fields (1143–1149)

**Interfaces:**
- Consumes: nothing new (the three new page functions are added in Task 12; this task adds the table with the **existing** pages and Task 12 appends rows).
- Produces: `typedef void (*PageFn)(); struct PageDef { const char* name; PageFn draw; }; const PageDef PAGES[]; int pageCount();` — Task 12 appends to `PAGES`.

- [ ] **Step 1: Dust gate**

After the `#include "observation.h"` line add:

```cpp
// The Grove Dust Sensor is temporarily out of the build (sky-observatory milestone). With
// it unplugged D0 floats and pollDust() would log noise, so 0 compiles out polling, the
// Dust page and its history; /gps.csv keeps its columns with empty dust fields.
#define DUST_ENABLED 0
```

In `setup()`, wrap the four dust lines:

```cpp
#if DUST_ENABLED
  pinMode(DUST_PIN, INPUT);
  dustWindowStart = millis();
  dustWasLow = (digitalRead(DUST_PIN) == LOW);
  dustFallAtUs = micros();
#endif
```

In `loop()`: wrap `pollDust();` and `pushDustHist(dustRatio);` each in `#if DUST_ENABLED … #endif`.

In `logRow()`, before the `f.printf(` call add:

```cpp
  char dustR[12] = "", dustC[12] = "";
#if DUST_ENABLED
  snprintf(dustR, sizeof(dustR), "%.2f", dustRatio);
  snprintf(dustC, sizeof(dustC), "%.1f", dustConc);
#endif
```

and in the format string change `%s,%.2f,%.1f,%.2f` (the `anom,dust_ratio,dust_conc,temp_c` part) to `%s,%s,%s,%.2f`, replacing the arguments `dustRatio, dustConc` with `dustR, dustC`.

- [ ] **Step 2: Page table and dispatch**

Replace the whole `drawPage()` function with:

```cpp
// ---------- page table ----------
// Add a page = add one row. Navigation: KEY_C and 5-way RIGHT go forward, 5-way LEFT back.
typedef void (*PageFn)();
struct PageDef { const char* name; PageFn draw; };
const PageDef PAGES[] = {
  {"Sky",    drawSky},
  {"Detail", drawDetail},
  {"Chart",  drawChart},
#if DUST_ENABLED
  {"Dust",   drawDust},
#endif
  {"Env",    drawEnv},
  {"Obs",    drawObs},
};
int pageCount() { return (int)(sizeof(PAGES) / sizeof(PAGES[0])); }

void drawPage() {
  spr.fillSprite(TFT_BLACK);
  drawHeader();
  drawSdBadge();
  PAGES[page].draw();
  if (anomalyActive()) {   // reception-health alert banner, over any page
    spr.fillRect(0, 224, 320, 16, TFT_RED);
    spr.setTextSize(2);
    spr.setTextColor(TFT_WHITE, TFT_RED);
    char m[24]; snprintf(m, sizeof(m), "! %s", anomCode);
    spr.drawString(m, 6, 225);
  }
  spr.pushSprite(0, 0);   // one atomic blit to the screen -> no flicker
}
```

- [ ] **Step 3: Buttons**

Replace `pollButtons()` with:

```cpp
bool prev5L = HIGH, prev5R = HIGH;

void gotoPage(int p) {
  int n = pageCount();
  page = ((p % n) + n) % n;
  if (screenOn) drawPage();
}

void pollButtons() {
  bool k = digitalRead(WIO_KEY_C);              // top-left button: next page (legacy)
  if (prevKeyC == HIGH && k == LOW) gotoPage(page + 1);
  prevKeyC = k;

  bool r = digitalRead(WIO_5S_RIGHT);           // 5-way right: next page
  if (prev5R == HIGH && r == LOW) gotoPage(page + 1);
  prev5R = r;

  bool l = digitalRead(WIO_5S_LEFT);            // 5-way left: previous page
  if (prev5L == HIGH && l == LOW) gotoPage(page - 1);
  prev5L = l;

  bool s5 = digitalRead(WIO_5S_PRESS);          // 5-way centre press: screen on/off
  if (prev5s == HIGH && s5 == LOW) setScreen(!screenOn);
  prev5s = s5;
}
```

In `setup()`, after `pinMode(WIO_5S_PRESS, INPUT_PULLUP);` add:

```cpp
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
```

- [ ] **Step 4: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [ ] **Step 5: Hardware checkpoint (user)**

Upload. Expected: five pages (Sky, Detail, Chart, Env, Obs), no Dust page; KEY_C and 5-way right advance, 5-way left goes back, wrap-around both ways; centre press still toggles the backlight. If left/right feel reversed in landscape, swap the two pin names in `pollButtons()` and note it in the comment.

- [ ] **Step 6: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "orbital-density: table-driven pages, 5-way navigation, compile-time dust gate"
```

---

### Task 12: Sensor pages (`pages_sensors.h`)

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/pages_sensors.h`
- Modify: `m2_skyview.ino` — include placed immediately above the `// ---------- page table ----------` comment (after the observation-log block); append three rows to `PAGES`

**Interfaces:**
- Consumes: `spr`, `gps`, all sensor globals (Tasks 7–8; Task 9 only under `MLX_ENABLED`), `i2cFound/i2cFoundCount` (Task 6), `loopMaxMsLast/loopIterLast` (Task 5), `obsRowsWritten/obsWriteErrors/obsOpen` (Task 10), `batOk`, `bmeOk`, `BME_ADDR`.
- Produces: `void drawSeries(const int16_t* v, int len, int X, int Y, int W, int H, int minSpan, uint16_t colour, const char* unit); void drawSkySensors(); void drawMag(); void drawSensors(); const char* skyConditionLabel();`

- [ ] **Step 1: Write `pages_sensors.h`**

```cpp
// pages_sensors.h — display pages for the sky-observatory sensors. Included from
// m2_skyview.ino AFTER the sensor headers, spr/gps, the observation-log block and the
// existing draw helpers, so it uses them directly (single translation unit).
// Same 8-bit sprite, same built-in font (size 1 = 6x8 px, size 2 = 12x16 px).
#pragma once

// 24 h line chart of a Ring<int16_t,N>.v holding value*10. Auto-scaled with a minimum span
// (in value*10 units); labels left of the frame; UTC hour markers below, like the old charts.
void drawSeries(const int16_t* v, int len, int X, int Y, int W, int H, int minSpan, uint16_t colour, const char* unit) {
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) { char t[4]; snprintf(t, sizeof(t), "%02d", hrs[i]); spr.drawString(t, xs[i], Y + H + 3); }
  } else {
    spr.drawString("-24h", X, Y + H + 3);
    spr.drawString("now", X + W - 18, Y + H + 3);
  }
  if (len < 2) { spr.drawString("collecting (1 sample / 5 min)", X + 40, Y + H / 2 - 4); return; }
  int lo = v[0], hi = v[0];
  for (int i = 1; i < len; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
  if (hi - lo < minSpan) { int mid = (hi + lo) / 2; lo = mid - minSpan / 2; hi = mid + minSpan / 2; }
  char lbl[16];
  spr.setTextColor(colour, TFT_BLACK);
  snprintf(lbl, sizeof(lbl), "%.1f%s", hi / 10.0f, unit); spr.drawString(lbl, 2, Y - 3);
  snprintf(lbl, sizeof(lbl), "%.1f%s", lo / 10.0f, unit); spr.drawString(lbl, 2, Y + H - 6);
  int px = -1, py = 0;
  for (int i = 0; i < len; i++) {
    int x = X + (int)((long)(W - 2) * i / (len - 1));
    int y = Y + H - 1 - (int)((long)(H - 2) * (v[i] - lo) / (hi - lo));
    if (px >= 0) spr.drawLine(px, py, x, y, colour);
    px = x; py = y;
  }
}

// PROVISIONAL sky-condition label from the thermal delta only. These thresholds are
// placeholders until field calibration produces a local baseline (README). Not a cloud index.
const float SKY_CLEAR_DELTA_C  = 20.0f;   // ambient - skyIR above this: probably clear
const float SKY_CLOUDY_DELTA_C = 8.0f;    // below this: probably overcast or obstructed

const char* skyConditionLabel() {
#if MLX_ENABLED
  if (!mlxOk || isnan(mlxDeltaC)) return "--";
  if (mlxDeltaC >= SKY_CLEAR_DELTA_C)  return "CLEAR?";
  if (mlxDeltaC <= SKY_CLOUDY_DELTA_C) return "CLOUDY?";
  return "MIXED?";
#else
  return "--";                 // needs the thermal channel (GY-906 deferred)
#endif
}

void drawSkySensors() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[48]; int y = 26;
  spr.setTextSize(2);
  if (!tslOk) {
    spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("TSL2591 missing", 8, y); y += 24;
  } else {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    if (isnan(tslLux))      snprintf(l, sizeof(l), "Visible  %s", tslSat ? "SATURATED" : "--");
    else if (tslLux < 1.0f) snprintf(l, sizeof(l), "Visible  %.4f lx", tslLux);
    else                    snprintf(l, sizeof(l), "Visible  %.1f lx", tslLux);
    spr.drawString(l, 8, y); y += 24;
    float irFrac = tslFull ? 100.0f * tslIr / tslFull : 0.0f;
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    snprintf(l, sizeof(l), "Near IR  %.1f %%", irFrac); spr.drawString(l, 8, y); y += 24;
    spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(l, sizeof(l), "raw full %u  ir %u  gain %s  %u ms", tslFull, tslIr, tslGainName(), tslIntegMs);
    spr.drawString(l, 8, y); y += 14;
    spr.setTextSize(2);
  }
#if MLX_ENABLED
  if (!mlxOk) {
    spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("MLX90614 missing", 8, y); y += 24;
  } else {
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(l, sizeof(l), "Sky IR   %.1f C", mlxObjC); spr.drawString(l, 8, y); y += 24;
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(l, sizeof(l), "Delta %.1f C amb %.1f", mlxDeltaC, mlxAmbC); spr.drawString(l, 8, y); y += 24;
  }
#else
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK); spr.drawString("Sky IR   not installed", 8, y); y += 24;
#endif
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(l, sizeof(l), "Condition %s", skyConditionLabel()); spr.drawString(l, 8, y); y += 20;
#if MLX_ENABLED
  spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("thermal delta, 24 h  (thresholds provisional)", 8, y); y += 12;
  int h = 224 - 12 - y;
  if (h >= 30) drawSeries(mlxDeltaHist.v, mlxDeltaHist.len, 42, y, 268, h, 50, TFT_YELLOW, "C");
#endif
}

void drawMag() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[64]; int y = 26;
  spr.setTextSize(2);
  if (!magOk) { spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("MMC5603 missing", 8, y); return; }
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(l, sizeof(l), "|B| %.2f uT", magMeanTotal); spr.drawString(l, 8, y);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(l, sizeof(l), "Hdg %.0f", magMeanHeading); spr.drawString(l, 200, y); y += 24;
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(l, sizeof(l), "X %7.2f  Y %7.2f", magMeanX, magMeanY); spr.drawString(l, 8, y); y += 24;
  snprintf(l, sizeof(l), "Z %7.2f", magMeanZ); spr.drawString(l, 8, y); y += 24;
  spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  snprintf(l, sizeof(l), "%lu samples  %lu err  off %.1f/%.1f/%.1f  level assumed",
           (unsigned long)magSampleCount, (unsigned long)magErrCount, MAG_OFF_X, MAG_OFF_Y, MAG_OFF_Z);
  spr.drawString(l, 8, y); y += 14;
  spr.drawString("|B| uT, 24 h", 8, y); y += 12;
  drawSeries(magTotalHist.v, magTotalHist.len, 42, y, 268, 224 - 12 - y, 20, TFT_MAGENTA, "");
}

void drawSensors() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[64]; int y = 26;
  spr.setTextSize(2); spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("SENSORS  I2C 100k", 8, y); y += 26;
  struct Row { const char* name; uint8_t addr; bool ok; uint32_t lastMs; };
  Row rows[] = {
    {"TSL2591 optical", 0x29,     tslOk, tslLastMs},
    {"MMC5603 mag",     0x30,     magOk, magLastMs},
#if MLX_ENABLED
    {"MLX90614 IR",     0x5A,     mlxOk, mlxLastMs},
#endif
    {"BME280 env",      BME_ADDR, bmeOk, millis()},
    {"BQ27441 batt",    0x55,     batOk, millis()},
  };
  spr.setTextSize(1);
  for (auto& r : rows) {
    spr.setTextColor(r.ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    unsigned long age = r.ok ? (millis() - r.lastMs) / 1000 : 0;
    snprintf(l, sizeof(l), "0x%02X %-16s %s  %lus ago", r.addr, r.name, r.ok ? "OK " : "---", age);
    spr.drawString(l, 8, y); y += 14;
  }
  y += 6;
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("boot scan:", 8, y);
  int x = 72;
  for (uint8_t i = 0; i < i2cFoundCount && x < 300; i++) { snprintf(l, sizeof(l), "0x%02X", i2cFound[i]); spr.drawString(l, x, y); x += 32; }
  y += 18;
  snprintf(l, sizeof(l), "loop max %lu ms   iter/s %lu", (unsigned long)loopMaxMsLast, (unsigned long)loopIterLast); spr.drawString(l, 8, y); y += 14;
  snprintf(l, sizeof(l), "NMEA pass %lu  fail %lu", (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum()); spr.drawString(l, 8, y); y += 14;
  snprintf(l, sizeof(l), "obs rows %lu  write err %lu  %s", (unsigned long)obsRowsWritten, (unsigned long)obsWriteErrors, obsOpen ? "open" : "closed"); spr.drawString(l, 8, y); y += 14;
#if MLX_ENABLED
  snprintf(l, sizeof(l), "tsl err %lu  mag err %lu  mlx err %lu", (unsigned long)tslErrCount, (unsigned long)magErrCount, (unsigned long)mlxErrCount);
#else
  snprintf(l, sizeof(l), "tsl err %lu  mag err %lu  (MLX90614 not installed)", (unsigned long)tslErrCount, (unsigned long)magErrCount);
#endif
  spr.drawString(l, 8, y);
}
```

- [ ] **Step 2: Include and register the pages**

Immediately above the `// ---------- page table ----------` comment add:

```cpp
#include "pages_sensors.h"     // SkySens / Mag / Sensors pages (needs spr, gps, sensors, obs stats)
```

Append to `PAGES[]` after `{"Obs",    drawObs},`:

```cpp
  {"SkySens", drawSkySensors},
  {"Mag",     drawMag},
  {"Sensors", drawSensors},
```

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [ ] **Step 4: Hardware checkpoint (user)**

Upload. Expected: eight pages; SkySens shows live lux / IR fraction, "Sky IR   not installed" and Condition `--`; Mag shows |B|, heading and X/Y/Z; Sensors lists four devices green with `Ns ago` ticking, the boot scan addresses, loop max, NMEA counters and obs rows climbing by ~1/s. Unplug a sensor: its row turns red within a second (TSL/MAG) and its page shows "missing". After 10+ minutes both new charts have their first points. Ask the user for three screenshots (`docs/page-skysens.jpg`, `docs/page-mag.jpg`, `docs/page-sensors.jpg`), EXIF-stripped like the existing ones.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview wio-terminal/orbital-density/docs/page-skysens.jpg wio-terminal/orbital-density/docs/page-mag.jpg wio-terminal/orbital-density/docs/page-sensors.jpg
git commit -m "orbital-density: add SkySens, Mag and Sensors pages"
```

---

### Task 13: Documentation

**Files:**
- Modify: `wio-terminal/orbital-density/README.md` (Hardware list 30–45; wiring sections 47–94; Firmware section incl. the "All six display pages" sentence at line 18 and the 0x76/0x77 claim at 85–87; libraries list 112–115)
- Modify: `wio-terminal/orbital-density/AGENTS.md` (structure list 5–10; libraries 31; refactor rule 37; testing 41)
- Modify: `wio-terminal/orbital-density/wio-sky-observatory-integration.md` (sensor table 9–16; GNSS screen 352–366)

**Interfaces:** none (docs only).

- [ ] **Step 1: README — hardware and wiring**

In the Hardware list add three bullets after the BME280 line:

```markdown
- Wio Terminal Chassis – Battery (650 mAh) with BQ27441 fuel gauge (I2C 0x55); adds 1 Grove UART,
  1 Grove I2C and 4 Grove analog/digital sockets fed from the 40-pin header.
- Adafruit TSL2591 (STEMMA QT) — optical sky brightness, visible + near-IR, I2C 0x29.
- Adafruit MMC5603 (STEMMA QT) — magnetometer on a 15–30 cm non-magnetic arm, I2C 0x30.
- MLX90614ESF-BCC on a GY-906 module — long-wave IR sky brightness temperature, SMBus 0x5A
  (3 V, 35° FOV, gradient compensated). **Deferred** until its header is soldered (`MLX_ENABLED 0`).
- Grove I2C Hub (4 sockets, passive) + 2× Adafruit 4528 Grove-to-STEMMA QT cable (or 1× plus a
  QT-to-QT cable) + 1 plain Grove cable for the GPS; Grove-to-female-jumper cable for the GY-906 later.
```

Rewrite the GNSS wiring section. Change its heading to `### GNSS wiring — chassis Grove UART socket (= Serial1)` and put this text before the existing header-pin table (keep the table and the D0/D1 warning below it as the fallback):

```markdown
The Air530Z plugs into the battery-chassis socket labelled **`RX TX`** (bottom edge, beside the
USB-C; labels are on the back of the chassis) with a plain Grove cable. Per Seeed's schematic that
socket is header pins 10/8 (RXD/TXD), i.e. `Serial1` — the same UART the firmware has always used,
so nothing changes in code. ⚠️ The four `IO*` sockets look identical and are plain GPIO: a GPS
plugged into one of them is silent (`firmware/m1_pinsweep` finds where its TX line really lands). Verified by the NMEA count printed
by `firmware/m1_i2c_scan`. Without the chassis, jumper-wire the module to the header:
```

Extend the D0/D1 warning with the reason it can never work in this firmware:

```markdown
Three things stack up against the right Grove port: the core maps D0/D1 as analog pins, so a
`Uart` needs an explicit `pinPeripheral(..., PIO_SERCOM_ALT)`; their only SERCOM is SERCOM4,
whose TX must be on pad 0 = D0 (the opposite of the Grove UART pin order, so the cable would
need TX/RX swapped); and the core's `Wire.cpp` already defines the SERCOM4 interrupt handlers
for `Wire1`, so any sketch that uses `Wire` and defines them too fails to link.
```

Change the BME280 heading to `### BME280 wiring — Grove I2C Hub socket` and its first paragraph to:

```markdown
The Seengreat BME280 plugs into one socket of the **Grove I2C Hub** on the left Grove port via
its Grove cable (same `Wire` bus as before). VCC = 3.3V, GND, SCL, SDA.
```

After the BME280 section add:

````markdown
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

⚠️ **The bus runs at 100 kHz and must stay there** — the MLX90614 is an SMBus device.
Each board carries its own pull-ups; if the boot scan (`I2C scan:` on USB serial, or the
Sensors page) misses a device intermittently, remove one board's pull-up resistors.
````

Fix the earlier claim: change "the firmware auto-detects both" (0x76/0x77) to "`m1_bme280` auto-detects both; `m2_skyview` is hard-coded to 0x77".

- [ ] **Step 2: README — firmware, pages, log, libraries, calibration**

Change "All six display pages" to "All eight display pages" and add the three new pages to the page descriptions with their screenshots:

```markdown
- **SkySens** — visible lux (or raw counts + gain when saturated/dark), near-IR fraction,
  Sky IR temperature, thermal delta, provisional condition label, 24 h delta chart. The thermal
  rows read "not installed" until the GY-906 is enabled.
- **Mag** — |B|, heading (level assumed), X/Y/Z means, 24 h |B| chart.
- **Sensors** — every I2C device with OK/missing and sample age, boot scan, loop max time,
  NMEA pass/fail counters, obs.csv row count.
```

Navigation: "KEY_C or 5-way right: next page; 5-way left: previous; 5-way centre: backlight."

Add an "Observation log" subsection:

```markdown
### Observation log — `/obs.csv` (1 Hz)

One row per second, header generated from `OBS_COLS` in `firmware/m2_skyview/observation.h`:
`utc, uptime_s, lat, lon, alt_m, fix, hdop, used, in_view, tsl_full, tsl_ir, tsl_vis, tsl_gain,
tsl_integ_ms, tsl_lux, tsl_sat, mlx_ambient_c, mlx_object_c, mlx_delta_c, mag_x_ut, mag_y_ut,
mag_z_ut, mag_total_ut, heading_deg, temp_c, humidity, pressure_hpa, sensors_ok, loop_max_ms`.
Absent sensors leave their fields empty. `sensors_ok` bits: 1 TSL, 2 MLX, 4 MAG, 8 BME.
About 15 MB/day. `OBS_EVERY_N` in `m2_skyview.ino` slows it down; `/gps.csv` is unchanged.
```

Libraries list: add `Adafruit MMC56x3` (and `Adafruit MLX90614 Library` once the GY-906 is enabled). The TSL2591 has a local register-level driver because the Adafruit library blocks for 720 ms per read.

Add a "Magnetometer calibration" subsection pointing at `tools/mag_calib.py` and the constants `MAG_OFF_X/Y/Z`, `MAG_MOUNT_OFFSET_DEG`, `MAG_DECLINATION_DEG` in `mmc5603.h` (procedure is in Task 14).

- [ ] **Step 3: AGENTS.md**

- Structure list: add `firmware/m1_i2c_scan/`, `m1_tsl2591/`, `m1_mmc5603/`, `m1_mlx90614/` bring-up entries; describe `m2_skyview/` as "the sketch plus header-only modules (`*.h`) in the same folder".
- Libraries line: append `Adafruit MMC56x3`, `Adafruit MLX90614 Library`.
- Replace the sentence "Avoid broad refactors in `m2_skyview.ino`; …" with:

```markdown
Keep existing GPS parsing, sky plot, chart and `/gps.csv` code in place; add new functionality
as header modules in `firmware/m2_skyview/` (pattern: `xxxOk`, `xxxInit()`, non-blocking
`xxxPoll()`), and limit edits to `m2_skyview.ino` to includes, `setup()`, `loop()`, the page
table and the observation log. Nothing in `loop()` may block: the UART buffer is 256 bytes.
```

- Testing: add "Check `loopMax` and `nmeaFail` in the 1 Hz status line against the baseline in README before and after any loop change; confirm `/obs.csv` rows for logging changes."
- Documentation line: mention `docs/superpowers/specs/` and `docs/superpowers/plans/`.

- [ ] **Step 4: Concept doc notes**

In `wio-sky-observatory-integration.md`: under the sensor table add "> SHT40 and the separate pressure sensor are superseded by the BME280 already in the firmware (decision 2026-09-02)." Under the GNSS screen mock-up add "> Galileo is not receivable on the Air530's AT6558R; the real page shows GPS/GLONASS/BeiDou/QZSS." Add to the I²C section: "> Implemented: see `docs/superpowers/specs/2026-09-02-sky-observatory-sensors-design.md` and README 'Sky-observatory sensors'."

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/README.md wio-terminal/orbital-density/AGENTS.md wio-terminal/orbital-density/wio-sky-observatory-integration.md wio-terminal/orbital-density/docs/superpowers
git commit -m "orbital-density docs: sky-observatory sensors wiring, pages, obs.csv, agent rules"
```

---

### Task 14: Field calibration tooling and procedure

**Files:**
- Create: `wio-terminal/orbital-density/tools/mag_calib.py`
- Modify: `wio-terminal/orbital-density/firmware/m2_skyview/mmc5603.h` (the five calibration constants)
- Modify: `wio-terminal/orbital-density/README.md` ("Magnetometer calibration" subsection)

**Interfaces:**
- Consumes: `/obs.csv` columns `mag_x_ut, mag_y_ut, mag_z_ut, mag_total_ut, uptime_s` (Task 10).
- Produces: numbers for `MAG_OFF_X/Y/Z`; a documented arm-length result; data for replacing `SKY_CLEAR_DELTA_C` / `SKY_CLOUDY_DELTA_C` later.

- [x] **Step 1: Write `tools/mag_calib.py`**

```python
#!/usr/bin/env python3
"""Magnetometer helpers for the orbital-density /obs.csv log.

  python3 tools/mag_calib.py offsets obs.csv [--start S --end S]
      Hard-iron offsets from a slow full rotation of the instrument: the midpoint of
      min/max on each axis. Paste the three numbers into MAG_OFF_X/Y/Z in mmc5603.h.

  python3 tools/mag_calib.py stability obs.csv [--start S --end S]
      Mean and standard deviation of |B| over a window: run once per arm length
      (15, 20, 25, 30 cm) while toggling the backlight and SD writes; pick the shortest
      arm whose std-dev stops improving.

--start/--end select rows by uptime_s so one file can hold several experiments.
"""
import argparse, csv, math, sys


def load(path, start, end):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                t = int(r["uptime_s"])
                x, y, z = float(r["mag_x_ut"]), float(r["mag_y_ut"]), float(r["mag_z_ut"])
            except (KeyError, ValueError):
                continue  # header mismatch or empty (sensor absent) fields
            if start is not None and t < start:
                continue
            if end is not None and t > end:
                continue
            rows.append((t, x, y, z))
    if not rows:
        sys.exit("no usable mag rows in the selected window")
    return rows


def offsets(rows):
    xs, ys, zs = zip(*[(r[1], r[2], r[3]) for r in rows])
    ox, oy, oz = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, (max(zs) + min(zs)) / 2
    print(f"rows={len(rows)}")
    print(f"MAG_OFF_X = {ox:.2f}f, MAG_OFF_Y = {oy:.2f}f, MAG_OFF_Z = {oz:.2f}f")
    print("(valid only if the rotation covered a full 360° in the level plane; "
          f"x span {max(xs)-min(xs):.1f}, y span {max(ys)-min(ys):.1f} uT should be similar)")


def stability(rows):
    tot = [math.sqrt(x * x + y * y + z * z) for _, x, y, z in rows]
    mean = sum(tot) / len(tot)
    sd = math.sqrt(sum((v - mean) ** 2 for v in tot) / len(tot))
    print(f"rows={len(rows)}  |B| mean={mean:.2f} uT  std={sd:.3f} uT  min={min(tot):.2f}  max={max(tot):.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["offsets", "stability"])
    ap.add_argument("csv")
    ap.add_argument("--start", type=int)
    ap.add_argument("--end", type=int)
    a = ap.parse_args()
    rows = load(a.csv, a.start, a.end)
    (offsets if a.mode == "offsets" else stability)(rows)


if __name__ == "__main__":
    main()
```

- [x] **Step 2: Smoke-test the script on a synthetic file**

```bash
python3 - <<'EOF'
import csv, math
with open("/tmp/obs_test.csv","w",newline="") as f:
    w=csv.writer(f); w.writerow(["uptime_s","mag_x_ut","mag_y_ut","mag_z_ut"])
    for i in range(360):
        a=math.radians(i); w.writerow([i, 5+20*math.cos(a), -3+20*math.sin(a), 40+0.1*math.sin(a)])
EOF
python3 wio-terminal/orbital-density/tools/mag_calib.py offsets /tmp/obs_test.csv
python3 wio-terminal/orbital-density/tools/mag_calib.py stability /tmp/obs_test.csv --start 0 --end 100
```

Expected: `MAG_OFF_X = 5.00f, MAG_OFF_Y = -3.00f, MAG_OFF_Z = 40.00f` (±0.01) and a stability line with rows=101.

- [ ] **Step 3: Hardware procedure (user) — arm length**

Ask the user to run the instrument outdoors or by a window with `/obs.csv` logging, and for each arm length 15, 20, 25, 30 cm: note `uptime_s` at the start, hold still for 2 minutes while toggling the backlight (5-way centre) every 20 s, note `uptime_s` at the end. Then per window:

```bash
python3 wio-terminal/orbital-density/tools/mag_calib.py stability obs.csv --start <s> --end <e>
```

Record the four std-dev values in README under "Magnetometer calibration" and state the chosen arm length.

- [ ] **Step 4: Hardware procedure (user) — hard-iron offsets**

With the chosen arm fixed, rotate the whole instrument slowly and level through 360° over ~60 s (note start/end `uptime_s`), then:

```bash
python3 wio-terminal/orbital-density/tools/mag_calib.py offsets obs.csv --start <s> --end <e>
```

Paste the three values into `mmc5603.h`, set `MAG_DECLINATION_DEG` for the site (e.g. from the NOAA magnetic-field calculator), and `MAG_MOUNT_OFFSET_DEG` so that the Mag page reads the true bearing of a known landmark. Compile and upload; record all five constants and the date in README.

- [ ] **Step 5: Optical/thermal baseline (user, ongoing)**

Leave the instrument logging over at least one clear night, one overcast night and one moonlit night. Note the `mlx_delta_c` ranges for each in README; only then replace `SKY_CLEAR_DELTA_C` / `SKY_CLOUDY_DELTA_C` in `pages_sensors.h` with values from the data (keep the `?` suffix on the label until humidity and pressure are folded in).

- [ ] **Step 6: Commit**

```bash
git add wio-terminal/orbital-density/tools/mag_calib.py wio-terminal/orbital-density/firmware/m2_skyview/mmc5603.h wio-terminal/orbital-density/README.md
git commit -m "orbital-density: magnetometer calibration tool and field-calibration constants"
```

---

## Self-review notes

- Spec §2 (wiring/parts/addresses) → Task 1 + Task 13. §3 (blocking constraints) → Tasks 2, 3, 5, 9 gate. §4.1–4.2 (files, module contract) → Tasks 5–9. §4.3–4.4 (Observation, column table) → Task 10. §4.5 (page table, 5-way) → Task 11. §4.6 (dust gate) → Task 11. §5 drivers → Tasks 2/7, 8, 9. §6 display → Task 12. §7 metric → Task 5. §8 logging → Task 10. §9 bring-up → Tasks 1–4. §10 verification → each task's checkpoint. §11 calibration → Task 14. §12 docs → Task 13.
- MMC5603: `magnetSetReset()` is never called after `begin()`; auto set/reset (CTRL0 `0x20`) replaces it in Tasks 3 and 8.
- `MLX_ENABLED` (Task 10) guards every MLX90614 reference in Tasks 9, 10 and 12; with the flag at 0 nothing from Task 9 is referenced.
- Names used across tasks: `tslOk/tslInit/tslPoll/tslFull/tslIr/tslGainName/tslIntegMs/tslLux/tslSat/tslLastMs/tslErrCount` (2→7→10→12); `magOk/magInit/magPoll/magRollSecond/magPushHist/magMean*/magTotalHist/magLastMs/magSampleCount/magErrCount/MAG_OFF_*` (8→10→12→14); `mlxOk/mlxInit/mlxPoll/mlxPushHist/mlxAmbC/mlxObjC/mlxDeltaC/mlxDeltaHist/mlxLastMs/mlxErrCount` (9→10→12); `i2cFound/i2cFoundCount/I2C_REPROBE_MS/I2C_CLOCK_HZ` (6→7→12); `loopMaxMsLast/loopIterLast` (5→10→12); `obs/assembleObservation/logObs/obsRowsWritten/obsWriteErrors/obsOpen/OBS_EVERY_N` (10→12→13); `PAGES/pageCount/gotoPage` (11→12).

# M5StickS3 Soil Moisture Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Wio Terminal soil moisture monitor to the M5StickS3 with feature parity: calibrated moisture %, drying rate, watering ETA, 48 h chart, alert chirp, reboot-surviving per-minute logging, screen toggle, trend reset.

**Architecture:** Two Arduino sketches under `m5sticks3/soil-moisture-monitor/firmware/`: `m1_bringup` (serial-only wiring/ADC validation) and `m2_monitor` (full port, built up across Tasks 3–6 in one file with a flash-and-verify cycle per task). Persistence uses LittleFS on internal flash instead of SD. Algorithms and constants are copied from `wio-terminal/soil-moisture-monitor/firmware/m5_power/m5_power.ino` — that file is the reference implementation; when in doubt, match it.

**Tech Stack:** arduino-cli, `esp32:esp32` core (ESP32-S3), M5Unified + M5GFX, LittleFS.

**Spec:** `docs/superpowers/specs/2026-08-17-m5sticks3-soil-moisture-monitor-design.md`

## Global Constraints

- Sensor is unit **#2** of the 5-pack: `SENSOR_ID = 2` everywhere (CSV, header text).
- Wiring: VCC→`3V3_L2`, GND→`GND`, AOUT→`G7` (GPIO 7, ADC1_CH6). Never use G2, G3, G43, G44, G0, G9, G10.
- CSV format identical to the Wio project: `boot,minute,sensor,raw,pct` + `#RESET,<minute>` markers, header row `boot,minute,sensor,raw,pct`.
- Algorithm constants identical to the Wio `m5_power` sketch: WATER_THRESHOLD 30.0, LOG_PERIOD_MS 60000, HISTORY_LEN 2880, RATE_WINDOW 360, RATE_MIN_SAMPLES 30, WATERING_JUMP 5.0, LONG_PRESS_MS 3000, RESTORE_TAIL_BYTES 90000, alert re-arm above threshold+5.
- Display: 240×135 landscape (`setRotation(1)`), same color semantics as the Wio (moisture red <30 / yellow <50 / green ≥50).
- Hardware steps (wiring, water dips, button presses, reading the screen) are performed by the user — stop and ask, never assume the outcome.
- All `arduino-cli` commands run from the repo root `/Users/george.babanau/repos/embedded`.
- FQBN (pinned in Task 1, expected value): `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB`
- Commit after every task. Do not push until the final task.

---

### Task 1: Toolchain + project scaffold + m1_bringup sketch

**Files:**
- Create: `m5sticks3/soil-moisture-monitor/firmware/m1_bringup/m1_bringup.ino`
- Create: `m5sticks3/soil-moisture-monitor/.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: pinned FQBN string + serial port path (record them in the task report AND as comments at the top of `m1_bringup.ino`); a compiling bring-up sketch. Later tasks reuse the FQBN/port verbatim.

- [ ] **Step 1: Install the ESP32 core**

```bash
arduino-cli core update-index && arduino-cli core install esp32:esp32
```

Expected: core installs (this downloads ~300 MB; allow several minutes).

- [ ] **Step 2: Verify the esp32s3 board options and pin the FQBN**

```bash
arduino-cli board details -b esp32:esp32:esp32s3 | grep -E "CDCOnBoot|FlashSize|PSRAM|PartitionScheme" -A3 | head -40
```

Confirm option keys `CDCOnBoot=cdc`, `FlashSize=8M`, `PSRAM=opi`, and a partition scheme with a SPIFFS/LittleFS data partition (`default_8MB` = 3.3 MB app / 1.5 MB SPIFFS — correct). If an option key differs from the Global Constraints FQBN, adjust the FQBN and record the corrected value; use it in ALL later compile/upload commands.

- [ ] **Step 3: Install libraries**

```bash
arduino-cli lib install M5Unified
```

Expected: installs M5Unified and its M5GFX dependency. (LittleFS ships with the core. If M5Unified reports a version older than 0.2.0, run `arduino-cli lib upgrade M5Unified`.)

- [ ] **Step 4: Write the bring-up sketch**

Create `m5sticks3/soil-moisture-monitor/firmware/m1_bringup/m1_bringup.ino`:

```cpp
// Milestone 1 — bring-up: raw ADC + millivolts over USB serial.
// Wiring (top HAT header): sensor VCC -> 3V3_L2, GND -> GND, AOUT -> G7.
// Validates: 3V3_L2 rail is live, wiring, air-vs-water delta, no clipping.
//
// FQBN: <filled in Task 1 Step 2>
// Port: <filled in Task 1 Step 7>

#include <M5Unified.h>

const int SENSOR_PIN = 7;  // G7, ADC1_CH6

uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {  // insertion sort -> median
    uint16_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  return s[N / 2];
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);  // powers rails, inits display
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);  // usable range ~0-3.1 V
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.drawString("m1 bringup - see serial", 4, 60);
}

void loop() {
  uint16_t raw = readSensorRaw();
  uint32_t mv = analogReadMilliVolts(SENSOR_PIN);
  Serial.printf("raw=%4u  mv=%4lu\n", raw, (unsigned long)mv);
  delay(1000);
}
```

- [ ] **Step 5: Create `.gitignore`**

Create `m5sticks3/soil-moisture-monitor/.gitignore` with the same content as the Wio project's (`wio-terminal/soil-moisture-monitor/.gitignore`) — copy it verbatim.

- [ ] **Step 6: Compile (this is the test gate)**

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" m5sticks3/soil-moisture-monitor/firmware/m1_bringup
```

Expected: `Sketch uses ... bytes` success output. If M5Unified fails to compile for ESP32-S3, report the exact error and stop — that's the spec's flagged risk, not something to work around silently.

- [ ] **Step 7: Identify the device port**

```bash
arduino-cli board list
```

Ask the user to unplug/replug the StickS3 if ambiguous; the port that disappears/reappears is it (expected: `/dev/cu.usbmodem*`). Record port + FQBN in the sketch header comment.

- [ ] **Step 8: Commit**

```bash
git add m5sticks3 && git commit -m "Add M5StickS3 m1 bring-up sketch"
```

---

### Task 2: Hardware validation + sensor #2 calibration

**Files:**
- Create: `m5sticks3/soil-moisture-monitor/CALIBRATION.md`

**Interfaces:**
- Consumes: FQBN + port from Task 1.
- Produces: `RAW_AIR` and `RAW_WATER` integer anchors for sensor #2, recorded in CALIBRATION.md. Task 3 copies them into `m2_monitor.ino`.

- [ ] **Step 1: User checkpoint — wiring**

Ask the user to seat the three jumpers per the wiring table (VCC→`3V3_L2`, GND→`GND`, AOUT→`G7` — pin names are on the device sticker) and confirm. Warn: the photo from design time suggested the yellow wire might be in G3 or 5V_IN — verify against the sticker.

- [ ] **Step 2: Flash m1_bringup**

```bash
arduino-cli upload --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" -p /dev/cu.usbmodemXXX m5sticks3/soil-moisture-monitor/firmware/m1_bringup
```

(Substitute the real port. If upload fails, ask the user to hold the side button long-press to enter download mode, then retry.)

- [ ] **Step 3: Verify rail + readings (test for this task)**

```bash
arduino-cli monitor -p /dev/cu.usbmodemXXX -c 115200 --timeout 15s
```

Expected: one `raw=...  mv=...` line per second. Interpret:
- `raw` pinned at 0 → 3V3_L2 rail likely dead or AOUT miswired. First re-check wiring with the user. If wiring is right, the rail is PMIC-gated: fall back to powering the sensor from a GPIO — move VCC to `G8`, add `pinMode(8, OUTPUT); digitalWrite(8, HIGH);` in `setup()` (the sensor draws ~5 mA, safe for one GPIO), re-flash, re-test. Record whichever supply worked in CALIBRATION.md and carry the same `pinMode/digitalWrite` lines into every later task's sketch.
- `raw` pinned at 4095 or `mv` flat ≈3100 in dry air → clipping; note it in CALIBRATION.md (air anchor sits at the ADC ceiling; calibration still works, the top of the range is just compressed).
- Jittering mid-range values in air → healthy.

- [ ] **Step 4: User checkpoint — air/water delta**

Ask the user to hold the probe in dry air ~10 s, then in a cup of water up to the insertion line ~10 s, while you run the monitor command again with `--timeout 30s`. Expected: stable air value, stable (lower) water value, delta of several hundred counts. If the delta is under ~200 counts, stop and report (clone-at-3.3V issue from the Wio project's PLAN.md §1 — sensor may need 5 V, which changes the design; do not improvise).

- [ ] **Step 5: Record calibration**

Take the median-ish stable values observed and write `m5sticks3/soil-moisture-monitor/CALIBRATION.md`, following the structure of `wio-terminal/soil-moisture-monitor/CALIBRATION.md` (read it first, mirror its sections) with: date, sensor **#2**, supply used (3V3_L2 or G8 fallback), `RAW_AIR = <air value>` (0 %), `RAW_WATER = <water value>` (100 %), the mv values, and any clipping note.

- [ ] **Step 6: Commit**

```bash
git add m5sticks3/soil-moisture-monitor/CALIBRATION.md && git commit -m "Calibrate sensor #2 on M5StickS3"
```

---

### Task 3: m2_monitor — live moisture display

**Files:**
- Create: `m5sticks3/soil-moisture-monitor/firmware/m2_monitor/m2_monitor.ino`

**Interfaces:**
- Consumes: `RAW_AIR`/`RAW_WATER` from CALIBRATION.md (Task 2); FQBN/port from Task 1.
- Produces: functions later tasks extend: `uint16_t readSensorRaw()`, `float rawToPercent(uint16_t)`, `void drawHeader()`, `void drawLive(float pct, uint16_t raw)`, constants `SENSOR_PIN`, `SENSOR_ID`, `WATER_THRESHOLD`. Layout coordinates: header y0, big % at (0,20) size 4, right column x104 (rate y24, ETA y44), sparkline box (2,64,236,44), diag row y124 size 1.

- [ ] **Step 1: Write the sketch**

Create `m5sticks3/soil-moisture-monitor/firmware/m2_monitor/m2_monitor.ino`. Replace `RAW_AIR`/`RAW_WATER` values with the anchors from CALIBRATION.md (the 3300/1500 below are stand-ins and MUST be replaced). If Task 2 used the G8 supply fallback, also add the two G8 lines noted there to `setup()`.

```cpp
// Soil moisture monitor for M5StickS3 — port of the Wio Terminal m5_power sketch.
// Wiring (top HAT header): sensor VCC -> 3V3_L2, GND -> GND, AOUT -> G7.
//
// FQBN: <from Task 1>
// Port: <from Task 1>

#include <M5Unified.h>

// ---- calibration (sensor #2, see CALIBRATION.md) ----
const uint16_t RAW_AIR   = 3300;  // 0 %   <- REPLACE from CALIBRATION.md
const uint16_t RAW_WATER = 1500;  // 100 % <- REPLACE from CALIBRATION.md
const int SENSOR_PIN = 7;         // G7, ADC1_CH6
const int SENSOR_ID  = 2;

// ---- behaviour (identical to the Wio m5_power sketch) ----
const float WATER_THRESHOLD = 30.0f;

uint32_t lastSenseMs = 0;

// ---------- sensor ----------

uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {
    uint16_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  return s[N / 2];
}

float rawToPercent(uint16_t raw) {
  float pct = 100.0f * (float)(RAW_AIR - raw) / (float)(RAW_AIR - RAW_WATER);
  return constrain(pct, 0.0f, 100.0f);
}

// ---------- display (240x135 landscape) ----------

void drawHeader() {
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("Soil Moisture #2", 0, 0);
}

void drawLive(float pct, uint16_t raw) {
  uint16_t color = pct < WATER_THRESHOLD ? TFT_RED
                 : (pct < 50 ? TFT_YELLOW : TFT_GREEN);
  char buf[8];
  snprintf(buf, sizeof(buf), "%3d%%", (int)(pct + 0.5f));
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.drawString(buf, 0, 20);

  char diag[48];
  uint32_t upMin = millis() / 60000;
  int bat = M5.Power.getBatteryLevel();  // 0-100, or <0 if unknown
  if (bat >= 0) {
    snprintf(diag, sizeof(diag), "raw %4u  bat %3d%%  up %lu:%02lu   ", raw,
             bat, (unsigned long)(upMin / 60), (unsigned long)(upMin % 60));
  } else {
    snprintf(diag, sizeof(diag), "raw %4u  bat --  up %lu:%02lu   ", raw,
             (unsigned long)(upMin / 60), (unsigned long)(upMin % 60));
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString(diag, 4, 124);
}

// ---------- main ----------

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);

  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  drawHeader();
}

void loop() {
  M5.update();

  if (millis() - lastSenseMs >= 1000) {
    lastSenseMs = millis();
    uint16_t raw = readSensorRaw();
    float pct = rawToPercent(raw);
    drawLive(pct, raw);
  }

  delay(25);
}
```

- [ ] **Step 2: Compile (test gate)**

```bash
arduino-cli compile --fqbn "<FQBN from Task 1>" m5sticks3/soil-moisture-monitor/firmware/m2_monitor
```

Expected: success.

- [ ] **Step 3: Flash and user checkpoint**

Upload (same command shape as Task 2 Step 2, pointing at `m2_monitor`). Ask the user: header "Soil Moisture #2" top-left, a large percentage updating every second (green if the probe is in wet soil/water, red in air), and a bottom line `raw NNNN  bat NN%  up 0:00`? A probe dip should move the % visibly.

- [ ] **Step 4: Commit**

```bash
git add m5sticks3/soil-moisture-monitor/firmware/m2_monitor && git commit -m "M5StickS3 m2: live calibrated moisture display"
```

---

### Task 4: m2_monitor — trends (ring buffer, rate, ETA, sparkline)

**Files:**
- Modify: `m5sticks3/soil-moisture-monitor/firmware/m2_monitor/m2_monitor.ino`

**Interfaces:**
- Consumes: Task 3's file and functions.
- Produces: `void commitToHistory(float pct)`, `bool slopePerHour(float &out)`, `void drawSparkline()`, `void drawTrend(float pct)`, globals `history[]`, `totalSamples`, `segmentStart`, `lastMean`, `accumPct`, `accumN`, `lastLogMs`, `minuteIdx`, `bootId`, `lowAlerted`; constants `LOG_PERIOD_MS`, `HISTORY_LEN`, `RATE_WINDOW`, `RATE_MIN_SAMPLES`, `WATERING_JUMP`. Task 5 hooks logging into the same once-per-minute block.

- [ ] **Step 1: Add constants and globals**

After the existing `WATER_THRESHOLD` line add:

```cpp
const uint32_t LOG_PERIOD_MS    = 60000;
const int      HISTORY_LEN      = 2880;    // 48 h of 1-min samples
const long     RATE_WINDOW      = 360;     // fit over at most 6 h
const long     RATE_MIN_SAMPLES = 30;
const float    WATERING_JUMP    = 5.0f;

float history[HISTORY_LEN];
long  totalSamples = 0;
long  segmentStart = 0;
int   bootId = 1;
long  minuteIdx = 0;
float accumPct = 0;
int   accumN = 0;
float lastMean = 0;
uint32_t lastLogMs = 0;
bool  lowAlerted = false;

float histAt(long absIdx) { return history[absIdx % HISTORY_LEN]; }
```

- [ ] **Step 2: Add trend functions**

Copy `commitToHistory` and `slopePerHour` from `wio-terminal/soil-moisture-monitor/firmware/m5_power/m5_power.ino:90-114` verbatim (they have no hardware dependencies).

- [ ] **Step 3: Add sparkline and trend rendering**

Add above `drawLive`:

```cpp
const int SPARK_X = 2, SPARK_Y = 64, SPARK_W = 236, SPARK_H = 44;

int sparkY(float pct) {
  return SPARK_Y + SPARK_H - 2 - (int)((SPARK_H - 4) * pct / 100.0f);
}

void drawSparkline() {
  M5.Display.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_BLACK);
  M5.Display.drawRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_DARKGREY);
  int yThr = sparkY(WATER_THRESHOLD);
  for (int x = SPARK_X + 2; x < SPARK_X + SPARK_W - 2; x += 6) {
    M5.Display.drawPixel(x, yThr, TFT_RED);
  }
  long span = min(totalSamples, (long)HISTORY_LEN);
  if (span < 2) return;
  long stride = max(1L, span / (long)(SPARK_W - 4));
  long cols = span / stride;
  int prevX = -1, prevY = 0;
  for (long c = 0; c < cols; c++) {
    long idx = totalSamples - span + c * stride;
    int x = SPARK_X + 2 + (int)((long)(SPARK_W - 4) * c / max(cols - 1, 1L));
    int y = sparkY(histAt(idx));
    if (prevX >= 0) M5.Display.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x; prevY = y;
  }
}

void drawTrend(float pct) {
  char line[32];
  M5.Display.setTextSize(2);

  float rate;
  bool haveRate = slopePerHour(rate);
  if (!haveRate) {
    long have = totalSamples - segmentStart;
    snprintf(line, sizeof(line), "%2ld/%ldm    ", have, RATE_MIN_SAMPLES);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString(line, 104, 24);
    M5.Display.drawString("ETA --     ", 104, 44);
    return;
  }

  snprintf(line, sizeof(line), "%+5.2f %%/h ", rate);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(line, 104, 24);

  if (pct <= WATER_THRESHOLD) {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("WATER NOW! ", 104, 44);
  } else if (rate >= -0.005f) {
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("not drying ", 104, 44);
  } else {
    float hours = (pct - WATER_THRESHOLD) / -rate;
    if (hours > 99 * 24) {
      snprintf(line, sizeof(line), ">99 days   ");
    } else {
      int d = (int)(hours / 24), h = (int)hours % 24;
      snprintf(line, sizeof(line), "~%dd %02dh   ", d, h);
    }
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(line, 104, 44);
  }
}
```

- [ ] **Step 4: Wire the once-per-minute commit into `loop()`**

Inside the existing 1 s block in `loop()`, after `drawLive(pct, raw);`, add accumulation and the minute block (port of `m5_power.ino:405-441`, minus SD and buzzer which arrive in Tasks 5–6):

```cpp
    accumPct += pct;
    accumN++;

    if (millis() - lastLogMs >= LOG_PERIOD_MS && accumN > 0) {
      lastLogMs += LOG_PERIOD_MS;
      float mean = accumPct / accumN;
      accumPct = 0;
      accumN = 0;
      lastMean = mean;

      commitToHistory(mean);
      minuteIdx++;

      drawSparkline();
      drawTrend(mean);
    }
```

Also add `lastLogMs = millis();`, `drawSparkline();` and `drawTrend(0);` at the end of `setup()` (after `drawHeader();`) so the empty chart frame and `0/30m` state render immediately.

- [ ] **Step 5: Compile (test gate)**

Same compile command. Expected: success.

- [ ] **Step 6: Flash and user checkpoint**

Ask the user to confirm: right column shows ` 0/30m` and `ETA --`, the chart frame with red dotted threshold line renders, and after 2–3 minutes the counter reads `2/30m`, `3/30m`… (full rate/ETA appears after 30 min — do not wait; the counter advancing is the pass signal).

- [ ] **Step 7: Commit**

```bash
git add m5sticks3/soil-moisture-monitor/firmware/m2_monitor && git commit -m "M5StickS3 m2: trends, rate, ETA, sparkline"
```

---

### Task 5: m2_monitor — LittleFS logging, restore, rotation, DUMP

**Files:**
- Modify: `m5sticks3/soil-moisture-monitor/firmware/m2_monitor/m2_monitor.ino`

**Interfaces:**
- Consumes: Task 4's minute block and globals.
- Produces: `bool fsOk`, `bool initFs()`, `bool appendLog(uint16_t raw, float pct)`, `void restoreHistory()`, `void rotateIfNeeded()`, `void handleSerial()`, `void drawFsBadge()`; constants `LOG_PATH = "/soil.csv"`, `OLD_PATH = "/soil.old.csv"`, `ROTATE_AT_BYTES = 1500000`, `RESTORE_TAIL_BYTES = 90000`. Task 6 calls `appendLog`-adjacent marker writes on trend reset.

- [ ] **Step 1: Add includes, constants, FS badge**

Top of file, after the M5Unified include: `#include <LittleFS.h>`. With the other constants:

```cpp
const char*    LOG_PATH  = "/soil.csv";
const char*    OLD_PATH  = "/soil.old.csv";
const uint32_t ROTATE_AT_BYTES    = 1500000;  // ~7 weeks per file
const uint32_t RESTORE_TAIL_BYTES = 90000;

bool fsOk = false;
```

Badge (top-right of the 240 px row, mirrors the Wio SD badge):

```cpp
void drawFsBadge() {
  M5.Display.setTextSize(2);
  if (fsOk) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString("FS ", 204, 0);
  } else {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("FS!", 204, 0);
  }
}
```

- [ ] **Step 2: Add filesystem functions**

```cpp
bool initFs() {
  fsOk = LittleFS.begin(true);  // true = format on first mount
  return fsOk;
}

void rotateIfNeeded() {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  uint32_t size = f.size();
  f.close();
  if (size < ROTATE_AT_BYTES) return;
  LittleFS.remove(OLD_PATH);
  LittleFS.rename(LOG_PATH, OLD_PATH);
  File nf = LittleFS.open(LOG_PATH, "w");
  if (nf) {
    nf.println("boot,minute,sensor,raw,pct");
    nf.close();
  }
}

bool appendLog(uint16_t raw, float pct) {
  File f = LittleFS.open(LOG_PATH, "a");
  if (!f) return false;
  f.printf("%d,%ld,%d,%u,%.2f\n", bootId, minuteIdx, SENSOR_ID, raw, pct);
  f.close();
  rotateIfNeeded();
  return true;
}

void restoreHistory() {
  if (!fsOk || !LittleFS.exists(LOG_PATH)) {
    if (fsOk) {
      File f = LittleFS.open(LOG_PATH, "w");
      if (f) { f.println("boot,minute,sensor,raw,pct"); f.close(); }
    }
    return;
  }
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  uint32_t size = f.size();
  if (size > RESTORE_TAIL_BYTES) {
    f.seek(size - RESTORE_TAIL_BYTES);
    f.readStringUntil('\n');
  }
  long lastBoot = 0, lastMinute = -1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.startsWith("#RESET")) {
      totalSamples = 0;  // probe was moved: discard everything before this
      continue;
    }
    int b, sensor;
    long m;
    unsigned int raw;
    float pct;
    if (sscanf(line.c_str(), "%d,%ld,%d,%u,%f", &b, &m, &sensor, &raw, &pct) == 5) {
      history[totalSamples % HISTORY_LEN] = pct;
      totalSamples++;
      lastBoot = b;
      lastMinute = m;
    }
  }
  f.close();
  bootId = (int)lastBoot + 1;
  minuteIdx = lastMinute + 1;
  segmentStart = totalSamples;  // trend restarts: off-time is unknown
  if (totalSamples > 0) lowAlerted = histAt(totalSamples - 1) <= WATER_THRESHOLD;
  Serial.printf("RESTORED,%ld,boot=%d\n", totalSamples, bootId);
}

void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd != "DUMP") return;
  for (const char* p : {OLD_PATH, LOG_PATH}) {
    File f = LittleFS.open(p, "r");
    if (!f) continue;
    while (f.available()) Serial.write(f.read());
    f.close();
  }
  Serial.println("#DUMP_END");
}
```

- [ ] **Step 3: Wire into `setup()` and `loop()`**

In `setup()`, after `drawHeader();`: `initFs(); restoreHistory(); drawFsBadge();` (keep the existing `drawSparkline(); drawTrend(...)` calls after these so restored history renders; change the `drawTrend(0)` call to `drawTrend(totalSamples > 0 ? histAt(totalSamples - 1) : 0);` and set `lastMean` the same way). In `loop()`, first line after `M5.update();`: `handleSerial();`. In the minute block, after `commitToHistory(mean);` and before `minuteIdx++;`:

```cpp
      if (!fsOk) initFs();
      if (fsOk && !appendLog(raw, mean)) fsOk = false;
      drawFsBadge();
```

- [ ] **Step 4: Compile (test gate)**

Same compile command. Expected: success.

- [ ] **Step 5: Flash and verify logging + restore**

Flash, wait ~2.5 minutes, then in a serial session send `DUMP` (run `arduino-cli monitor -p <port> -c 115200`, type `DUMP`, Enter). Expected: header row plus ≥2 CSV rows with `sensor` column = 2, ending `#DUMP_END`. Then ask the user to unplug/replug the device (or single-click the side reset): after reboot the serial prints `RESTORED,<n>,boot=2` and the right column shows ` 0/30m` (trend restarted) while the sparkline still shows the restored points. Green `FS` badge visible top-right.

- [ ] **Step 6: Commit**

```bash
git add m5sticks3/soil-moisture-monitor/firmware/m2_monitor && git commit -m "M5StickS3 m2: LittleFS logging, restore, rotation, DUMP"
```

---

### Task 6: m2_monitor — controls, chirp, screen toggle

**Files:**
- Modify: `m5sticks3/soil-moisture-monitor/firmware/m2_monitor/m2_monitor.ino`

**Interfaces:**
- Consumes: everything prior; M5Unified `M5.BtnA` = KEY1/G11, `M5.BtnB` = KEY2/G12.
- Produces: final sketch. `void chirp()`, `void setScreen(bool on)`, `void restartTrends()`, globals `screenOn`, `keyBFired`.

- [ ] **Step 1: Add controls code**

Globals: `bool screenOn = true; bool keyBFired = false;`

```cpp
void chirp() {
  M5.Speaker.tone(1800, 120);
  delay(180);
  M5.Speaker.tone(1400, 200);
}

void setScreen(bool on) {
  screenOn = on;
  if (on) {
    M5.Display.wakeup();
    M5.Display.setBrightness(80);
    M5.Display.fillScreen(TFT_BLACK);
    drawHeader();
    drawFsBadge();
    drawSparkline();
    drawTrend(lastMean);
  } else {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  }
}

// After moving the probe: forget the old spot's history, mark it in the log.
void restartTrends() {
  totalSamples = 0;
  segmentStart = 0;
  accumPct = 0;
  accumN = 0;
  if (fsOk) {
    File f = LittleFS.open(LOG_PATH, "a");
    if (f) { f.printf("#RESET,%ld\n", minuteIdx); f.close(); }
  }
  Serial.printf("TRENDS_RESET,%ld\n", minuteIdx);
  chirp();
  drawSparkline();
  drawTrend(lastMean);
}

void pollButtons() {
  if (M5.BtnA.wasClicked()) setScreen(!screenOn);

  if (M5.BtnB.pressedFor(3000)) {
    if (!keyBFired) { keyBFired = true; restartTrends(); }
  } else if (M5.BtnB.wasReleased()) {
    keyBFired = false;
  }
}
```

- [ ] **Step 2: Gate all drawing on `screenOn` and wire everything up**

- Add `if (!screenOn) return;` as the first line of `drawHeader`, `drawFsBadge`, `drawSparkline`, `drawTrend`, and `drawLive` (matches the Wio's `backlightOn` guards — sampling and logging continue with the screen off).
- In `loop()`, after `handleSerial();`: `pollButtons();`.
- In the minute block, after the existing `drawTrend(mean);`, add the alert (port of `m5_power.ino:435-440`):

```cpp
      if (mean <= WATER_THRESHOLD && !lowAlerted) {
        lowAlerted = true;
        chirp();
      } else if (mean > WATER_THRESHOLD + 5) {
        lowAlerted = false;
      }
```

- [ ] **Step 3: Compile (test gate)**

Same compile command. Expected: success.

- [ ] **Step 4: Flash and user checkpoint**

Flash, then ask the user to verify, in order: (1) KEY1 (front button) click turns the screen off; a second click brings the full dashboard back. (2) Holding KEY2 for 3 s chirps through the speaker and the right column returns to ` 0/30m` — and a subsequent `DUMP` shows a `#RESET,<minute>` row. (3) With the probe lifted into dry air, within ~2 minutes the % drops below 30, turns red, `WATER NOW!` appears, and one chirp sounds. Probe back in soil/water afterwards.

- [ ] **Step 5: Commit**

```bash
git add m5sticks3/soil-moisture-monitor/firmware/m2_monitor && git commit -m "M5StickS3 m2: screen toggle, trend reset, alert chirp"
```

---

### Task 7: Documentation + push

**Files:**
- Create: `m5sticks3/soil-moisture-monitor/README.md`
- Modify: `README.md` (repo root — add a project-table row)

**Interfaces:**
- Consumes: everything shipped in Tasks 1–6, CALIBRATION.md values, pinned FQBN/port.

- [ ] **Step 1: Read the reference README**

Read `wio-terminal/soil-moisture-monitor/README.md` in full — the new README mirrors its structure and tone exactly: intro paragraph, Hardware, wiring table, Firmware (two-row milestone table: `m1_bringup`, `m2_monitor` ← current), controls paragraph (KEY1 click = screen toggle, KEY2 hold 3 s = trend reset), build/flash commands (real FQBN and port from Task 1, plus the "close the serial monitor first" warning), **Display section** (same table format as the Wio's, adjusted: `FS`/`FS!` badge instead of SD, `#2` sensor, `bat NN%` field, condensed ETA strings `ETA --` / `not drying` / `~Dd HHh` / `>99 days` / `WATER NOW!`), Data section (LittleFS `/soil.csv`, rotation to `/soil.old.csv` at ~1.5 MB ≈ 7 weeks each, `DUMP` serial command, pipe into `../../wio-terminal/soil-moisture-monitor/tools/export_report.py`), To do (link to the Wio README's to-dos rather than duplicating), Repo layout.

- [ ] **Step 2: Write the README**

Write `m5sticks3/soil-moisture-monitor/README.md` per Step 1. State the M5StickS3 specifics up front: ESP32-S3, 3.3 V logic, HAT header wiring table (VCC→3V3_L2 or G8 per what Task 2 recorded, GND→GND, AOUT→G7), no SD slot → internal-flash logging, 250 mAh battery.

- [ ] **Step 3: Add the root README row**

In the repo-root `README.md` project table add:

```markdown
| [m5sticks3/soil-moisture-monitor](m5sticks3/soil-moisture-monitor/) | M5StickS3 | active |
```

- [ ] **Step 4: Verify docs match reality**

Cross-check every value in the new README against the final sketch: pin numbers, FQBN, button names, ETA strings, rotation threshold, file paths. Fix mismatches in the README (the code is the source of truth at this point).

- [ ] **Step 5: Commit and push**

```bash
git add m5sticks3 README.md && git commit -m "Add M5StickS3 soil moisture monitor docs" && git push
```

Expected: push to `origin main` succeeds (remote `git@github.spoe8008:maker-lab-9000/embedded-projects.git`).

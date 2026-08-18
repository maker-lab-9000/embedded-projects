# M5StickS3 WiFi Presence Scanner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone portable "device radar" on the M5StickS3 — passively sniff 2.4 GHz WiFi and show on the LCD how many active devices are nearby and how close, on battery, no network connection.

**Architecture:** Two Arduino sketches under `m5sticks3/wifi-presence-scanner/firmware/`: `m1_sniff` (serial-only promiscuous-mode proof) and `m2_radar` (full UI, built up across Tasks 3–5 with a flash-and-verify cycle per task). The radio runs receive-only in promiscuous mode, hopping channels; a device table with a 60 s sliding window feeds the display. Reuses the display/controls/battery patterns proven in the sibling `m5sticks3/soil-moisture-monitor/firmware/m2_monitor` sketch — read it as the reference for M5Unified idioms.

**Tech Stack:** arduino-cli, `esp32:esp32` core (ESP32-S3), M5Unified, `esp_wifi.h` promiscuous APIs.

**Spec:** `docs/superpowers/specs/2026-08-18-m5sticks3-wifi-presence-scanner-design.md`

## Global Constraints

- FQBN (verbatim in every compile/upload): `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB`
- Port: `/dev/cu.usbmodem*` (native USB CDC). **Opening the serial port resets the device**; the reader must hold DTR and RTS inactive (`s.rts=False; s.dtr=False` before/after open). After every flash the chip parks in download mode — **the user single-clicks the side button to boot**. If it sits dark with USB product "ESP32_S3", it's in download mode: single-click to boot; if a flash fails to connect, the user long-presses the side button to force download mode, then retry.
- Receive-only. The firmware MUST NOT transmit: no `esp_wifi_connect`, no deauth, no injection, no `esp_wifi_80211_tx`. Station mode is started (`WIFI_MODE_STA`) but never connected.
- No persistence: nothing written to flash, no LittleFS. MACs live in RAM only, expired after 60 s. Never display or serialize a full MAC in `m2_radar` (m1_sniff may print MAC prefixes for debug).
- Constants: `CHANNEL_MIN=1`, `CHANNEL_MAX=13`, `DWELL_MS=250`, `WINDOW_MS=60000`, `TABLE_LEN=128`, `NEAR_DBM=-55`, `NEARBY_DBM=-75` (headline = devices with rssi ≥ NEARBY_DBM), `RSSI_ALPHA=0.3`, `SPARK_PERIOD_MS=15000`, `LONG_PRESS_MS=3000`.
- Display 240×135 landscape (`setRotation(1)`); colors: nearby count 0 dark grey / 1–3 green / 4–9 yellow / ≥10 red. Layout coords mirror the soil m2_monitor: header y0 size 2, big count (0,20) size 4, right column x104 (y24/y44) size 2, sparkline box (2,64,236,44), diag row (4,124) size 1.
- All `arduino-cli` commands run from repo root `/Users/george.babanau/repos/embedded`. Commit after every task; do not push until the final task.

---

### Task 1: Project scaffold + m1_sniff (promiscuous capture over serial)

**Files:**
- Create: `m5sticks3/wifi-presence-scanner/firmware/m1_sniff/m1_sniff.ino`
- Create: `m5sticks3/wifi-presence-scanner/.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: a compiling sniffer that classifies frames and prints a per-sweep summary. Establishes the frame-parsing helpers reused conceptually in Task 3: `classifyFrame()` logic (AP / prober / client) and RSSI extraction from `wifi_pkt_rx_ctrl_t`.

- [ ] **Step 1: Create `.gitignore`**

Copy `m5sticks3/soil-moisture-monitor/.gitignore` verbatim to `m5sticks3/wifi-presence-scanner/.gitignore` (contents: `firmware/*/build/`, `.DS_Store`, `*.uf2`, `report.md`).

- [ ] **Step 2: Write the sniffer sketch**

Create `m5sticks3/wifi-presence-scanner/firmware/m1_sniff/m1_sniff.ino`:

```cpp
// Milestone 1 — WiFi sniffer bring-up: promiscuous capture over USB serial.
// Receive-only. Hops channels 1-13, prints a per-sweep summary of frames seen,
// device kinds (AP / prober / client), and strongest RSSI. Validates the radio,
// channel hopping, and frame classification before building the UI.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem* (opening the port resets the device)

#include <M5Unified.h>
#include <WiFi.h>
#include "esp_wifi.h"

const int CHANNEL_MIN = 1, CHANNEL_MAX = 13;
const uint32_t DWELL_MS = 250;

volatile uint32_t frameCount = 0;
volatile uint32_t apFrames = 0, proberFrames = 0, clientFrames = 0;
volatile int8_t   strongestRssi = -128;

// 802.11 frame control: type/subtype from the first two bytes of the MAC header.
void onRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  int8_t rssi = p->rx_ctrl.rssi;
  const uint8_t* payload = p->payload;
  uint8_t fc0 = payload[0];
  uint8_t ftype = (fc0 >> 2) & 0x3;
  uint8_t fsubtype = (fc0 >> 4) & 0xF;

  frameCount++;
  if (rssi > strongestRssi) strongestRssi = rssi;

  if (ftype == 0) {  // management
    if (fsubtype == 8 || fsubtype == 5) apFrames++;       // beacon / probe-resp
    else if (fsubtype == 4) proberFrames++;               // probe-req
  } else if (ftype == 2) {  // data
    clientFrames++;
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("m1 sniff", 4, 4);

  WiFi.mode(WIFI_MODE_STA);   // station mode, never connected
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&onRx);
  esp_wifi_set_channel(CHANNEL_MIN, WIFI_SECOND_CHAN_NONE);
}

void loop() {
  static int ch = CHANNEL_MIN;
  static uint32_t sweepFrames = 0, sweepAp = 0, sweepProber = 0, sweepClient = 0;
  static int8_t sweepStrongest = -128;

  frameCount = 0; apFrames = 0; proberFrames = 0; clientFrames = 0;
  strongestRssi = -128;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  delay(DWELL_MS);

  Serial.printf("ch %2d: frames=%3lu ap=%2lu prober=%2lu client=%2lu rssi=%d\n",
                ch, (unsigned long)frameCount, (unsigned long)apFrames,
                (unsigned long)proberFrames, (unsigned long)clientFrames,
                (int)strongestRssi);

  sweepFrames += frameCount; sweepAp += apFrames;
  sweepProber += proberFrames; sweepClient += clientFrames;
  if (strongestRssi > sweepStrongest) sweepStrongest = strongestRssi;

  ch++;
  if (ch > CHANNEL_MAX) {
    Serial.printf("SWEEP total=%lu ap=%lu prober=%lu client=%lu strongest=%d\n",
                  (unsigned long)sweepFrames, (unsigned long)sweepAp,
                  (unsigned long)sweepProber, (unsigned long)sweepClient,
                  (int)sweepStrongest);
    ch = CHANNEL_MIN;
    sweepFrames = sweepAp = sweepProber = sweepClient = 0;
    sweepStrongest = -128;
  }
}
```

- [ ] **Step 3: Compile (test gate)**

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" m5sticks3/wifi-presence-scanner/firmware/m1_sniff
```

Expected: `Sketch uses ... bytes` success. If `esp_wifi_set_promiscuous_rx_cb` or the packet types don't resolve, report the exact error and stop.

- [ ] **Step 4: Commit**

```bash
git add m5sticks3/wifi-presence-scanner && git commit -m "Add M5StickS3 WiFi sniffer bring-up sketch"
```

---

### Task 2: Flash m1_sniff and validate capture

**Files:** none (hardware validation).

**Interfaces:**
- Consumes: m1_sniff from Task 1.

- [ ] **Step 1: Flash**

```bash
arduino-cli upload --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" -p /dev/cu.usbmodemXXX m5sticks3/wifi-presence-scanner/firmware/m1_sniff
```

(Real port. If it fails to connect, ask the user to long-press the side button to force download mode, then retry. After success, ask the user to single-click the side button to boot.)

- [ ] **Step 2: Capture serial (test for this task)**

Read serial WITHOUT resetting mid-capture, using the DTR/RTS-inactive Python reader pattern (as used throughout the soil project):

```python
import serial, time
s = serial.Serial(); s.port='/dev/cu.usbmodemXXX'; s.baudrate=115200; s.timeout=1
s.rts=False; s.dtr=False; s.open()
end=time.time()+20; buf=b''
while time.time()<end: buf+=s.read(256)
s.close(); print(buf.decode(errors='replace'))
```

Expected: `ch NN: frames=...` lines cycling through channels, and `SWEEP total=...` lines. Interpret:
- Beacons (`ap=`) should be non-zero on at least the channel(s) your home APs use (commonly 1, 6, 11).
- `frames=0` on every channel → radio not capturing; re-check the sketch flashed and the board booted (not stuck in download mode).

- [ ] **Step 3: User checkpoint — reactivity**

Ask the user to hold a phone next to the stick and actively browse (load a page). Re-run the 20 s capture. Expected: `client` and/or `prober` counts rise and `rssi` climbs toward −40…−30 (very close). Confirms the sniffer sees real nearby activity, not just distant beacons.

- [ ] **Step 4: Commit (marker only)**

No code change; record validation in the commit for m2 later. Skip if nothing to commit.

---

### Task 3: m2_radar — device table, sliding window, serial counts

**Files:**
- Create: `m5sticks3/wifi-presence-scanner/firmware/m2_radar/m2_radar.ino`

**Interfaces:**
- Consumes: frame-classification logic from m1_sniff.
- Produces: the device-table core reused by Tasks 4–5: struct `Device {uint8_t mac[6]; uint32_t lastSeenMs; float rssiEma; uint8_t kind;}` (kind: 1=AP, 2=prober, 3=client); `void tableUpsert(const uint8_t* mac, int8_t rssi, uint8_t kind)`; `void expireOld()`; counting helpers `int countNearby()`, `int countTotalDevices()`, `int countAPs()` — all reading the table under the `tableMux` critical section. Globals `Device table[TABLE_LEN]`, `volatile uint32_t framesSinceTick`.

- [ ] **Step 1: Write the sketch (core, no display yet beyond a text count)**

Create `m5sticks3/wifi-presence-scanner/firmware/m2_radar/m2_radar.ino`:

```cpp
// WiFi presence scanner (device radar) for M5StickS3 — receive-only.
// Counts nearby active WiFi devices via promiscuous-mode sniffing.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem* (opening the port resets the device)

#include <M5Unified.h>
#include <WiFi.h>
#include "esp_wifi.h"

const int      CHANNEL_MIN = 1, CHANNEL_MAX = 13;
const uint32_t DWELL_MS    = 250;
const uint32_t WINDOW_MS   = 60000;
const int      TABLE_LEN   = 128;
const int      NEAR_DBM    = -55;
const int      NEARBY_DBM  = -75;
const float    RSSI_ALPHA  = 0.3f;

struct Device { uint8_t mac[6]; uint32_t lastSeenMs; float rssiEma; uint8_t kind; };
Device table[TABLE_LEN];
int tableCount = 0;
portMUX_TYPE tableMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t framesSinceTick = 0;

int findMac(const uint8_t* mac) {
  for (int i = 0; i < tableCount; i++)
    if (memcmp(table[i].mac, mac, 6) == 0) return i;
  return -1;
}

// Called under tableMux.
void tableUpsert(const uint8_t* mac, int8_t rssi, uint8_t kind) {
  uint32_t now = millis();
  int i = findMac(mac);
  if (i < 0) {
    if (tableCount < TABLE_LEN) {
      i = tableCount++;
    } else {  // evict oldest
      i = 0;
      for (int j = 1; j < tableCount; j++)
        if (table[j].lastSeenMs < table[i].lastSeenMs) i = j;
    }
    memcpy(table[i].mac, mac, 6);
    table[i].rssiEma = rssi;
    table[i].kind = kind;
  } else {
    table[i].rssiEma = RSSI_ALPHA * rssi + (1 - RSSI_ALPHA) * table[i].rssiEma;
    if (kind == 3) table[i].kind = 3;  // data-frame client is the strongest signal
  }
  table[i].lastSeenMs = now;
}

void onRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  int8_t rssi = p->rx_ctrl.rssi;
  const uint8_t* d = p->payload;
  uint8_t fc0 = d[0];
  uint8_t ftype = (fc0 >> 2) & 0x3;
  uint8_t fsubtype = (fc0 >> 4) & 0xF;
  const uint8_t* src = d + 10;  // Address 2 (transmitter) for mgmt/data frames

  uint8_t kind = 0;
  if (ftype == 0) {
    if (fsubtype == 8 || fsubtype == 5) kind = 1;       // AP
    else if (fsubtype == 4) kind = 2;                    // prober
  } else if (ftype == 2) {
    kind = 3;                                            // client
  }
  if (kind == 0) return;
  if (src[0] & 0x01) return;  // skip group/broadcast source (shouldn't be, but guard)

  framesSinceTick++;
  portENTER_CRITICAL(&tableMux);
  tableUpsert(src, rssi, kind);
  portEXIT_CRITICAL(&tableMux);
}

void expireOld() {  // called under tableMux
  uint32_t now = millis();
  int w = 0;
  for (int i = 0; i < tableCount; i++) {
    if (now - table[i].lastSeenMs <= WINDOW_MS) {
      if (w != i) table[w] = table[i];
      w++;
    }
  }
  tableCount = w;
}

int countNearby() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++)
    if (table[i].kind != 1 && table[i].rssiEma >= NEARBY_DBM) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}
int countTotalDevices() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++) if (table[i].kind != 1) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}
int countAPs() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++) if (table[i].kind == 1) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}

uint32_t lastTickMs = 0, lastHopMs = 0;
int channel = CHANNEL_MIN;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&onRx);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void loop() {
  M5.update();

  if (millis() - lastHopMs >= DWELL_MS) {
    lastHopMs = millis();
    channel++;
    if (channel > CHANNEL_MAX) channel = CHANNEL_MIN;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }

  if (millis() - lastTickMs >= 1000) {
    lastTickMs = millis();
    portENTER_CRITICAL(&tableMux);
    expireOld();
    portEXIT_CRITICAL(&tableMux);
    uint32_t fps = framesSinceTick;
    framesSinceTick = 0;
    Serial.printf("nearby=%d devs=%d aps=%d fps=%lu ch=%d\n",
                  countNearby(), countTotalDevices(), countAPs(),
                  (unsigned long)fps, channel);
  }
}
```

- [ ] **Step 2: Compile (test gate)**

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" m5sticks3/wifi-presence-scanner/firmware/m2_radar
```

Expected: success.

- [ ] **Step 3: Flash and validate counts over serial**

Flash, single-click to boot, then run the 20 s DTR-inactive reader. Ask the user to have a phone nearby and active. Expected: `nearby=` rises with a nearby active phone and decays to ~0 within 60 s of removing it; `aps=` is a stable plausible number for the location; `fps=` non-zero.

- [ ] **Step 4: Commit**

```bash
git add m5sticks3/wifi-presence-scanner/firmware/m2_radar && git commit -m "M5StickS3 radar: device table, sliding window, serial counts"
```

---

### Task 4: m2_radar — display (counts, channel, sparkline, diag)

**Files:**
- Modify: `m5sticks3/wifi-presence-scanner/firmware/m2_radar/m2_radar.ino`

**Interfaces:**
- Consumes: Task 3 counting helpers and globals.
- Produces: `void drawAll()`, `void drawSparkline()`, `void drawCounts()`, `void drawDiag()`; globals `int spark[SPARK_N]` (SPARK_N=236), `int sparkLen`, `uint32_t lastSparkMs`; constant `SPARK_PERIOD_MS=15000`. Task 5 gates these on `screenOn`.

- [ ] **Step 1: Add display constants and sparkline buffer**

After the existing constants:

```cpp
const uint32_t SPARK_PERIOD_MS = 15000;   // 236 samples * 15 s ~= 59 min
const int SPARK_X = 2, SPARK_Y = 64, SPARK_W = 236, SPARK_H = 44;
const int SPARK_N = SPARK_W;  // one sample per column max

int spark[SPARK_N];
int sparkLen = 0;
uint32_t lastSparkMs = 0;
```

- [ ] **Step 2: Add draw functions**

```cpp
uint16_t countColor(int n) {
  if (n == 0) return TFT_DARKGREY;
  if (n <= 3) return TFT_GREEN;
  if (n <= 9) return TFT_YELLOW;
  return TFT_RED;
}

void drawCounts() {
  char line[24];
  int nearby = countNearby();

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("WiFi Radar", 0, 0);
  snprintf(line, sizeof(line), "ch %2d ", channel);
  M5.Display.drawString(line, 188, 0);

  snprintf(line, sizeof(line), "%3d near ", nearby);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(countColor(nearby), TFT_BLACK);
  M5.Display.drawString(line, 0, 20);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(line, sizeof(line), "%3d devs ", countTotalDevices());
  M5.Display.drawString(line, 104, 24);
  snprintf(line, sizeof(line), "%2d APs ", countAPs());
  M5.Display.drawString(line, 104, 44);
}

void drawSparkline() {
  M5.Display.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_BLACK);
  M5.Display.drawRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_DARKGREY);
  if (sparkLen < 2) return;
  int mx = 5;
  for (int i = 0; i < sparkLen; i++) if (spark[i] > mx) mx = spark[i];
  int prevX = -1, prevY = 0;
  for (int i = 0; i < sparkLen; i++) {
    int x = SPARK_X + 2 + (int)((long)(SPARK_W - 4) * i / (SPARK_N - 1));
    int y = SPARK_Y + SPARK_H - 2 - (int)((SPARK_H - 4) * spark[i] / mx);
    if (prevX >= 0) M5.Display.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x; prevY = y;
  }
}

void drawDiag(uint32_t fps) {
  char diag[48];
  uint32_t upMin = millis() / 60000;
  int bat = M5.Power.getBatteryLevel();
  if (bat >= 0)
    snprintf(diag, sizeof(diag), "fps %3lu  bat %3d%%  up %lu:%02lu  ",
             (unsigned long)fps, bat, (unsigned long)(upMin/60), (unsigned long)(upMin%60));
  else
    snprintf(diag, sizeof(diag), "fps %3lu  bat --  up %lu:%02lu  ",
             (unsigned long)fps, (unsigned long)(upMin/60), (unsigned long)(upMin%60));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString(diag, 4, 124);
}

void pushSpark(int v) {
  if (sparkLen < SPARK_N) {
    spark[sparkLen++] = v;
  } else {
    for (int i = 1; i < SPARK_N; i++) spark[i-1] = spark[i];
    spark[SPARK_N-1] = v;
  }
}
```

- [ ] **Step 3: Wire drawing into loop() and setup()**

In `setup()`, after `esp_wifi_set_channel(...)`: nothing needed (screen cleared already). Replace the `Serial.printf(...)` block inside the 1 Hz tick in `loop()` with drawing + the same serial line:

```cpp
    uint32_t fps = framesSinceTick;
    framesSinceTick = 0;
    drawCounts();
    drawDiag(fps);
    if (millis() - lastSparkMs >= SPARK_PERIOD_MS) {
      lastSparkMs = millis();
      pushSpark(countNearby());
      drawSparkline();
    }
    Serial.printf("nearby=%d devs=%d aps=%d fps=%lu ch=%d\n",
                  countNearby(), countTotalDevices(), countAPs(),
                  (unsigned long)fps, channel);
```

Also call `drawSparkline();` once at the end of `setup()` to paint the empty box immediately.

- [ ] **Step 4: Compile (test gate)**

Same compile command. Expected: success.

- [ ] **Step 5: Flash and user checkpoint**

Flash, single-click to boot. Ask the user to confirm: "WiFi Radar" header with a live `ch NN` that changes; a big `N near` count (color per bands) that rises when a phone is active nearby; `devs`/`APs` on the right; the sparkline box drawing; `fps NN  bat NN%  up H:MM` at the bottom. After ~15 s the sparkline gets its first point; over a couple minutes it traces the nearby count.

- [ ] **Step 6: Commit**

```bash
git add m5sticks3/wifi-presence-scanner/firmware/m2_radar && git commit -m "M5StickS3 radar: LCD counts, channel, sparkline, diagnostics"
```

---

### Task 5: m2_radar — controls (screen toggle, window reset, chirp)

**Files:**
- Modify: `m5sticks3/wifi-presence-scanner/firmware/m2_radar/m2_radar.ino`

**Interfaces:**
- Consumes: Task 4 draw functions; M5Unified `M5.BtnA` (KEY1), `M5.BtnB` (KEY2).
- Produces: final sketch. `void chirp()`, `void setScreen(bool)`, `void resetWindow()`, `void pollButtons()`, globals `bool screenOn`, `bool keyBFired`.

- [ ] **Step 1: Add controls**

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
    drawSparkline();  // counts/diag repaint on next tick
  } else {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  }
}

void resetWindow() {
  portENTER_CRITICAL(&tableMux);
  tableCount = 0;
  portEXIT_CRITICAL(&tableMux);
  sparkLen = 0;
  if (screenOn) drawSparkline();
  chirp();
  Serial.println("WINDOW_RESET");
}

void pollButtons() {
  if (M5.BtnA.wasClicked()) setScreen(!screenOn);
  if (M5.BtnB.pressedFor(3000)) {
    if (!keyBFired) { keyBFired = true; resetWindow(); }
  } else if (M5.BtnB.wasReleased()) {
    keyBFired = false;
  }
}
```

- [ ] **Step 2: Gate draws on screenOn and wire pollButtons**

- Add `if (!screenOn) return;` as the first line of `drawCounts`, `drawSparkline`, and `drawDiag`.
- In `loop()`, right after `M5.update();`: `pollButtons();`.
- The 1 Hz tick keeps running with the screen off (scanning + expiry continue); the guarded draws simply no-op.

- [ ] **Step 3: Compile (test gate)**

Same compile command. Expected: success.

- [ ] **Step 4: Flash and user checkpoint**

Flash, single-click to boot. Ask the user to verify: (1) KEY1 click blanks the screen; the serial `nearby=...` line keeps printing (scanning alive); KEY1 again restores the dashboard. (2) KEY2 held 3 s chirps and zeroes the counts + clears the sparkline (`WINDOW_RESET` on serial).

- [ ] **Step 5: Commit**

```bash
git add m5sticks3/wifi-presence-scanner/firmware/m2_radar && git commit -m "M5StickS3 radar: screen toggle, window reset, chirp"
```

---

### Task 6: Documentation + push

**Files:**
- Create: `m5sticks3/wifi-presence-scanner/README.md`
- Modify: `README.md` (repo root — project-table row)

**Interfaces:**
- Consumes: everything from Tasks 1–5.

- [ ] **Step 1: Read the reference README**

Read `m5sticks3/soil-moisture-monitor/README.md` for structure/tone. The radar README mirrors it: intro, Hardware (M5StickS3, no sensor needed — it's the radio), Firmware (two-row milestone table: `m1_sniff`, `m2_radar` ← current), controls paragraph (KEY1 screen toggle, KEY2 reset, side button = system/download), build+flash commands (real FQBN/port + the download-mode/single-click notes and "opening serial resets the device"), a **Display section** table (header + `ch`, big `N near`, `devs`, `APs`, sparkline, `fps`/`bat`/`up`) with value ranges and the color bands, a **What it measures / limitations** section (nearby = RSSI ≥ −75 dBm; MAC randomization means it's an activity meter not a census; client vs prober; 128-device cap; ~2–3 h battery), and a **Privacy** section (receive-only, no transmit, no logging, MACs in RAM ≤ 60 s, aggregates only). No Data/CSV section — nothing is logged.

- [ ] **Step 2: Write the README**

Write `m5sticks3/wifi-presence-scanner/README.md` per Step 1, values matching the final sketch (constants from Global Constraints, layout, ETA of battery).

- [ ] **Step 3: Add the root README row**

In the repo-root `README.md` project table add:

```markdown
| [m5sticks3/wifi-presence-scanner](m5sticks3/wifi-presence-scanner/) | M5StickS3 | active |
```

- [ ] **Step 4: Verify docs match reality**

Cross-check README values against the final sketch: constants, pin/button names, color bands, serial line formats, battery estimate. Fix mismatches in the README.

- [ ] **Step 5: Commit and push**

```bash
git add m5sticks3/wifi-presence-scanner README.md && git commit -m "Add M5StickS3 WiFi presence scanner docs" && git push
```

Expected: push to `origin main` succeeds.

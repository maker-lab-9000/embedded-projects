# Orbital Density (Wio Terminal) — Design & Implementation Plan

> **For agentic workers:** implement task-by-task; each task ends with a compile gate and (where noted) a hardware checkpoint. Steps use checkbox (`- [ ]`) syntax.

**Project vision:** "Orbital Density" — a Wio Terminal that visualizes what's *above* it. This plan implements the first milestone: **GPS sky view** (satellite density in the sky via the Air530). Planned later milestones add a **dust/particulate sensor** and an **ambient-light sensor** to build a fuller picture of the overhead environment; the firmware and CSV are structured so those slot in as additional sensors + pages/columns rather than a rewrite.

**Goal (this milestone):** Read the Air530 GNSS module on the Wio Terminal and show a live "sky radar" — a polar plot of every satellite in view (all constellations, color-coded), plus a numeric detail page and once-a-minute logging of satellite counts to microSD.

**Architecture:** NMEA 0183 streams from the Air530 into `Serial1` @ 9600. Two parsers share the stream: **TinyGPS++** for standard fields (fix, lat/lon/alt, HDOP, satellites *used*, UTC), and a small **custom GSV parser** for the per-satellite table (constellation, PRN, elevation, azimuth, SNR) that TinyGPS++ doesn't expose. The table drives the sky plot and counts. Two Arduino sketches: `m1_gps` (serial-only bring-up) and `m2_skyview` (full UI + logging).

**Tech Stack:** arduino-cli, `Seeeduino:samd` core, TinyGPSPlus, Seeed_FS (SD), TFT_eSPI (bundled with the Seeed core).

## Global Constraints

- FQBN (verbatim): `Seeeduino:samd:seeed_wio_terminal`.
- Air530 → the Wio's **40-pin header** UART (`Serial1.begin(9600)`), NOT a Grove port: GPS TX → header pin 10 (BCM15/RXD), VCC → pin 1 (3V3), GND → pin 6. The D0/D1 Grove port has no usable hardware UART (SERCOM4 won't latch it even though the signal is present — verified at bring-up). Module needs sky/window view; cold start 1–2 min.
- NMEA talker → constellation: `GP`=GPS, `GL`=GLONASS, `GA`=Galileo, `BD`/`GB`=BeiDou, `QZ`=QZSS. Validate the `*` checksum before trusting a sentence.
- "In view" = live GSV table entries (satellites the receiver hears). "Used" = `gps.satellites.value()` from GGA (in the fix). Keep the two distinct everywhere.
- Display 320×240 landscape (`tft.setRotation(3)`), reuse the visual language of the soil monitor (header, SD badge, dark background).
- Hardware steps (placing the module for sky view, reading the screen, pressing buttons, checking the SD file) are the user's — stop and ask, never assume.
- Commands run from repo root `/Users/george.babanau/repos/embedded`. Commit after every task; do not push until the final task.

## Repo layout

```
wio-terminal/orbital-density/
  README.md
  .gitignore
  firmware/m1_gps/m1_gps.ino          # Serial1 NMEA read + TinyGPS++ summary (serial)
  firmware/m2_skyview/m2_skyview.ino  # sky plot + detail page + SD logging
```

---

### Task 1: Scaffold + m1_gps (Serial1 NMEA read over serial)

**Files:**
- Create: `wio-terminal/orbital-density/.gitignore`
- Create: `wio-terminal/orbital-density/firmware/m1_gps/m1_gps.ino`

**Interfaces:**
- Produces: proof that Serial1 receives NMEA and TinyGPS++ parses it. Establishes the read loop (`feedGps()`) reused later.

- [ ] **Step 1: Install TinyGPS++**

```bash
arduino-cli lib install TinyGPSPlus
```

- [ ] **Step 2: `.gitignore`**

Create `wio-terminal/orbital-density/.gitignore` (copy the soil project's):

```
firmware/*/build/
.DS_Store
*.uf2
```

- [ ] **Step 3: Write m1_gps**

Create `wio-terminal/orbital-density/firmware/m1_gps/m1_gps.ino`:

```cpp
// Milestone 1 — Air530 GPS bring-up on the Wio Terminal.
// Reads NMEA from Serial1 (Grove UART @ 9600), echoes raw sentences and a
// TinyGPS++ parsed summary over USB serial. Validates wiring, baud, reception.
//
// FQBN: Seeeduino:samd:seeed_wio_terminal
// Air530 -> Wio UART Grove port (Serial1).

#include <TinyGPSPlus.h>

TinyGPSPlus gps;
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);   // Air530 default
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
    Serial.write(c);      // echo raw NMEA so we can see sentences
  }

  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    Serial.print("\n== fix=");
    Serial.print(gps.location.isValid() ? "yes" : "no");
    Serial.print(" satsUsed=");
    Serial.print(gps.satellites.isValid() ? (int)gps.satellites.value() : -1);
    Serial.print(" hdop=");
    Serial.print(gps.hdop.isValid() ? gps.hdop.hdop() : -1.0, 1);
    Serial.print(" chars=");
    Serial.print(gps.charsProcessed());
    Serial.print(" sentences=");
    Serial.print(gps.sentencesWithFix());
    Serial.println(" ==");
  }
}
```

- [ ] **Step 4: Compile (test gate)**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m1_gps
```

Expected: `Sketch uses ... bytes`.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/.gitignore wio-terminal/orbital-density/firmware/m1_gps
git commit -m "Add Wio GPS sky-view m1 bring-up sketch"
```

---

### Task 2: Flash m1_gps and validate reception

**Files:** none (hardware).

- [ ] **Step 1: Flash**

```bash
arduino-cli board list   # find the Wio's Seeeduino port
arduino-cli upload --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodemXXX wio-terminal/orbital-density/firmware/m1_gps
```

- [ ] **Step 2: User checkpoint — placement**

Ask the user to place the Air530 antenna with a clear view of the sky or on a windowsill, and note a cold start can take 1–2 minutes.

- [ ] **Step 3: Validate over serial (test)**

Read serial (~30 s). Expected: raw `$GxGSV`, `$GxGGA`, `$GxRMC` sentences streaming; `chars=` climbing (proves Serial1 wiring/baud); over a minute or two `satsUsed` rises and `fix=yes` once locked. Interpret: `chars=0` → wrong port/baud/wiring (try the other Grove port, or 115200); sentences but `fix=no` for long → needs better sky view. Note which Grove port worked in the m1 header comment.

---

### Task 3: m2_skyview core — GSV parser, satellite table, serial dump

**Files:**
- Create: `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino`

**Interfaces:**
- Produces: `enum Constel {C_GPS,C_GLO,C_GAL,C_BDS,C_QZS,C_OTHER}`; `struct Sat { uint8_t constel; uint8_t prn; int8_t elev; int16_t azim; uint8_t snr; uint32_t seen; }`; `Sat sats[MAX_SATS]` (MAX_SATS=64); `void feedGps()`; `void parseGsv(const char* s)`; `int countInView()`; `int countConstel(uint8_t)`; `const char* fixStr()`. Used by Tasks 4–6.

- [ ] **Step 1: Write the core sketch (parser + table + serial dump, no display yet)**

Create `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino`:

```cpp
// GPS Sky View for the Wio Terminal + Air530 — polar satellite plot, detail
// page, and per-minute logging. NMEA from Serial1 @ 9600.
//
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <TinyGPSPlus.h>

TinyGPSPlus gps;

enum Constel { C_GPS, C_GLO, C_GAL, C_BDS, C_QZS, C_OTHER };
struct Sat { uint8_t constel; uint8_t prn; int8_t elev; int16_t azim; uint8_t snr; uint32_t seen; };

const int      MAX_SATS   = 64;
const uint32_t SAT_TTL_MS = 10000;   // drop a satellite not reported for 10 s
Sat sats[MAX_SATS];
int satCount = 0;

// ---- NMEA line assembly (for the custom GSV parser) ----
char line[128];
int  linePos = 0;

uint8_t talkerConstel(const char* s) {  // s points at '$'; chars 1..2 = talker
  if (s[1] == 'G' && s[2] == 'P') return C_GPS;
  if (s[1] == 'G' && s[2] == 'L') return C_GLO;
  if (s[1] == 'G' && s[2] == 'A') return C_GAL;
  if (s[1] == 'B' && s[2] == 'D') return C_BDS;
  if (s[1] == 'G' && s[2] == 'B') return C_BDS;
  if (s[1] == 'Q' && s[2] == 'Z') return C_QZS;
  return C_OTHER;
}

bool checksumOk(const char* s) {       // s from '$' to end (has *HH)
  const char* star = strchr(s, '*');
  if (!star || strlen(star) < 3) return false;
  uint8_t sum = 0;
  for (const char* p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
  uint8_t want = (uint8_t)strtol(star + 1, nullptr, 16);
  return sum == want;
}

void upsertSat(uint8_t constel, uint8_t prn, int elev, int azim, int snr) {
  if (prn == 0) return;
  uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < satCount; i++)
    if (sats[i].constel == constel && sats[i].prn == prn) { slot = i; break; }
  if (slot < 0) {
    if (satCount < MAX_SATS) slot = satCount++;
    else return;  // table full (rare)
    sats[slot].constel = constel; sats[slot].prn = prn;
  }
  sats[slot].elev = elev; sats[slot].azim = azim;
  sats[slot].snr = snr < 0 ? 0 : snr;
  sats[slot].seen = now;
}

// $xxGSV,total,msgNum,inView,{prn,elev,azim,snr}x up to 4 ,...*cs
void parseGsv(const char* s) {
  if (!checksumOk(s)) return;
  uint8_t constel = talkerConstel(s);
  // tokenise by comma into fields[]
  char buf[128]; strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  char* star = strchr(buf, '*'); if (star) *star = 0;
  const int MAXF = 24; char* f[MAXF]; int nf = 0;
  for (char* p = strtok(buf, ","); p && nf < MAXF; p = strtok(nullptr, ",")) f[nf++] = p;
  // f[0]=$xxGSV f[1]=total f[2]=msg f[3]=inView, then groups of 4 from f[4]
  for (int i = 4; i + 3 < nf; i += 4) {
    int prn  = atoi(f[i]);
    int elev = f[i+1][0] ? atoi(f[i+1]) : -1;
    int azim = f[i+2][0] ? atoi(f[i+2]) : 0;
    int snr  = f[i+3][0] ? atoi(f[i+3]) : 0;
    if (elev >= 0) upsertSat(constel, prn, elev, azim, snr);
  }
}

void expireSats() {
  uint32_t now = millis();
  int w = 0;
  for (int i = 0; i < satCount; i++)
    if (now - sats[i].seen <= SAT_TTL_MS) { if (w != i) sats[w] = sats[i]; w++; }
  satCount = w;
}

void feedGps() {
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
    if (c == '\n') {
      line[linePos] = 0;
      if (linePos > 6 && !strncmp(line + 3, "GSV", 3)) parseGsv(line);
      linePos = 0;
    } else if (c != '\r' && linePos < (int)sizeof(line) - 1) {
      line[linePos++] = c;
    }
  }
}

int countInView() { return satCount; }
int countConstel(uint8_t c) { int n = 0; for (int i=0;i<satCount;i++) if (sats[i].constel==c) n++; return n; }
const char* fixStr() {
  if (!gps.location.isValid()) return "NO FIX";
  return gps.altitude.isValid() ? "3D FIX" : "2D FIX";
}

uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
}

void loop() {
  feedGps();
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    expireSats();
    Serial.printf("inView=%d used=%d %s | GPS=%d GLO=%d GAL=%d BDS=%d\n",
      countInView(), gps.satellites.isValid()?(int)gps.satellites.value():-1, fixStr(),
      countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS));
  }
}
```

- [ ] **Step 2: Compile (test gate)**

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio-terminal/orbital-density/firmware/m2_skyview
```

- [ ] **Step 3: Flash + validate the table over serial**

Flash, place for sky view. Expected serial: `inView=N used=M 3D FIX | GPS=.. GLO=.. GAL=.. BDS=..` with `inView` ≥ `used`, per-constellation counts plausible for the location, and the counts changing as reception settles.

- [ ] **Step 4: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "Wio GPS sky-view m2: GSV parser, satellite table, serial counts"
```

---

### Task 4: m2_skyview — sky plot page

**Files:**
- Modify: `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino`

**Interfaces:**
- Produces: `void drawSky()`, `uint16_t constelColor(uint8_t)`, `void drawHeader()`, layout constants (center `CX=160, CY=128`, radius `R=100`). Reused by Task 5's page switch.

- [ ] **Step 1: Add TFT + drawing**

Add near the top: `#include <TFT_eSPI.h>` and `TFT_eSPI tft;`. Add drawing constants and functions before `setup()`:

```cpp
const int CX = 160, CY = 128, R = 100;

uint16_t constelColor(uint8_t c) {
  switch (c) {
    case C_GPS: return TFT_GREEN;
    case C_GLO: return TFT_CYAN;
    case C_GAL: return TFT_ORANGE;
    case C_BDS: return TFT_MAGENTA;
    case C_QZS: return TFT_YELLOW;
    default:    return TFT_LIGHTGREY;
  }
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 20, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char h[40];
  snprintf(h, sizeof(h), "View %2d  Use %2d  %s",
           countInView(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0, fixStr());
  tft.drawString(h, 4, 2);
}

void drawLegend() {
  const char* names[] = {"GPS","GLO","GAL","BDS"};
  uint8_t cs[] = {C_GPS,C_GLO,C_GAL,C_BDS};
  tft.setTextSize(1);
  int y = 26;
  for (int i = 0; i < 4; i++) {
    tft.fillRect(288, y, 8, 8, constelColor(cs[i]));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(names[i], 300, y);
    y += 12;
  }
}

void drawSky() {
  tft.fillRect(0, 20, 320, 220, TFT_BLACK);
  // rings: horizon (elev 0 = R), 30, 60
  tft.drawCircle(CX, CY, R, TFT_DARKGREY);
  tft.drawCircle(CX, CY, R * 2 / 3, TFT_DARKGREY);
  tft.drawCircle(CX, CY, R / 3, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("N", CX - 2, CY - R - 10);
  tft.drawString("S", CX - 2, CY + R + 2);
  tft.drawString("E", CX + R + 2, CY - 3);
  tft.drawString("W", CX - R - 10, CY - 3);
  drawLegend();
  for (int i = 0; i < satCount; i++) {
    float r = R * (90 - sats[i].elev) / 90.0f;
    float a = sats[i].azim * 0.017453292f;   // deg->rad
    int x = CX + (int)(r * sinf(a));
    int y = CY - (int)(r * cosf(a));
    int rad = sats[i].snr >= 40 ? 4 : (sats[i].snr >= 25 ? 3 : 2);
    tft.fillCircle(x, y, rad, constelColor(sats[i].constel));
  }
}
```

- [ ] **Step 2: Init TFT in setup and draw each refresh**

In `setup()` after `Serial1.begin(9600);`:

```cpp
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
```

Replace the serial-only `loop()` refresh block so it also redraws (keep the serial line for debugging):

```cpp
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    expireSats();
    drawHeader();
    drawSky();
  }
```

- [ ] **Step 3: Compile (test gate)** — same compile command.

- [ ] **Step 4: Flash + user checkpoint**

Ask the user to confirm: a circular sky radar with 3 rings + N/E/S/W labels, colored dots for satellites (green GPS, cyan GLONASS, orange Galileo, magenta BeiDou) that move slowly over minutes, a header `View NN Use MM 3D FIX`, and the legend. Dots near the center = high in the sky, near the rim = low on the horizon.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "Wio GPS sky-view m2: polar sky plot"
```

---

### Task 5: m2_skyview — detail page + button toggle

**Files:**
- Modify: `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino`

**Interfaces:**
- Produces: `void drawDetail()`, page state, `void pollButton()`. `WIO_KEY_C` toggles pages.

- [ ] **Step 1: Add page state + detail renderer + button**

Globals: `bool detailPage = false; bool prevKeyC = HIGH;`

```cpp
int strongestSnr() { int m=0; for (int i=0;i<satCount;i++) if (sats[i].snr>m) m=sats[i].snr; return m; }

void drawDetail() {
  tft.fillRect(0, 20, 320, 220, TFT_BLACK);
  tft.setTextSize(2);
  char l[48]; int y = 30;
  auto row = [&](const char* s){ tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); tft.drawString(s, 8, y); y += 24; };
  snprintf(l, sizeof(l), "In view: %d", countInView()); row(l);
  snprintf(l, sizeof(l), "GPS %d GLO %d", countConstel(C_GPS), countConstel(C_GLO)); row(l);
  snprintf(l, sizeof(l), "GAL %d BDS %d", countConstel(C_GAL), countConstel(C_BDS)); row(l);
  snprintf(l, sizeof(l), "Used: %d  %s",
           gps.satellites.isValid()?(int)gps.satellites.value():0, fixStr()); row(l);
  snprintf(l, sizeof(l), "HDOP: %.1f", gps.hdop.isValid()?gps.hdop.hdop():0.0); row(l);
  if (gps.location.isValid()) snprintf(l, sizeof(l), "%.4f %.4f", gps.location.lat(), gps.location.lng());
  else snprintf(l, sizeof(l), "lat/lon: --");
  row(l);
  if (gps.altitude.isValid()) snprintf(l, sizeof(l), "Alt %.0fm  SNR %d", gps.altitude.meters(), strongestSnr());
  else snprintf(l, sizeof(l), "Alt --  SNR %d", strongestSnr());
  row(l);
  if (gps.time.isValid()) snprintf(l, sizeof(l), "UTC %02d:%02d:%02d",
                                   gps.time.hour(), gps.time.minute(), gps.time.second());
  else snprintf(l, sizeof(l), "UTC --");
  row(l);
}

void pollButton() {
  bool k = digitalRead(WIO_KEY_C);
  if (prevKeyC == HIGH && k == LOW) { detailPage = !detailPage; tft.fillScreen(TFT_BLACK); }
  prevKeyC = k;
}
```

- [ ] **Step 2: Wire in**

In `setup()`: `pinMode(WIO_KEY_C, INPUT_PULLUP);`. In `loop()`, add `pollButton();` right after `feedGps();`. Change the refresh block to branch:

```cpp
    drawHeader();
    if (detailPage) drawDetail(); else drawSky();
```

(Poll the button every loop, not just on the 1 s tick, so it feels responsive.)

- [ ] **Step 3: Compile (test gate)** — same compile command.

- [ ] **Step 4: Flash + user checkpoint**

Ask the user: pressing the top-left button (KEY_C) toggles to a numeric detail page (in view, per-constellation, used, fix, HDOP, lat/lon, alt, UTC, strongest SNR) and back to the sky plot. Values are live and match the sky.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "Wio GPS sky-view m2: detail page + button toggle"
```

---

### Task 6: m2_skyview — microSD logging

**Files:**
- Modify: `wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino`

**Interfaces:**
- Produces: `bool sdOk`, `void initSd()`, `void drawSdBadge()`, `void logRow()`; CSV `LOG_PATH="/gps.csv"`.

- [ ] **Step 1: Add SD includes + logging**

Add includes: `#include <Seeed_FS.h>` and `#include "SD/Seeed_SD.h"`. Globals: `bool sdOk=false; const char* LOG_PATH="/gps.csv"; uint32_t lastLogMs=0; const uint32_t LOG_PERIOD_MS=60000;`

```cpp
void initSd() {
  sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  if (sdOk && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) { f.println("utc,uptime_s,in_view,used,fix,hdop,gps,glonass,galileo,beidou"); f.close(); }
  }
}

void drawSdBadge() {
  tft.setTextSize(1);
  tft.setTextColor(sdOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(sdOk ? "SD" : "SD!", 300, 2);
}

void logRow() {
  if (!sdOk) { initSd(); if (!sdOk) return; }
  File f = SD.open(LOG_PATH, FILE_APPEND);
  if (!f) { sdOk = false; return; }
  char utc[24] = "";
  if (gps.date.isValid() && gps.time.isValid())
    snprintf(utc, sizeof(utc), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  f.printf("%s,%lu,%d,%d,%s,%.1f,%d,%d,%d,%d\n",
           utc, (unsigned long)(millis()/1000), countInView(),
           gps.satellites.isValid()?(int)gps.satellites.value():0,
           gps.location.isValid() ? (gps.altitude.isValid()?"3D":"2D") : "none",
           gps.hdop.isValid()?gps.hdop.hdop():0.0,
           countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS));
  f.close();
}
```

- [ ] **Step 2: Wire in**

In `setup()` after `tft.fillScreen`: `initSd();`. In the 1 s refresh block, after drawing, add the SD badge and the once-a-minute log:

```cpp
    drawSdBadge();
    if (millis() - lastLogMs >= LOG_PERIOD_MS) {
      lastLogMs = millis();
      logRow();
    }
```

- [ ] **Step 3: Compile (test gate)** — same compile command.

- [ ] **Step 4: Flash + validate logging**

Flash. Confirm the green `SD` badge (red `SD!` if no card). Wait ~2.5 min, then have the user pull the card (or check via a card reader) that `/gps.csv` has header + rows with plausible `in_view,used,fix,hdop` and per-constellation counts; `utc` populated once the module has a time fix.

- [ ] **Step 5: Commit**

```bash
git add wio-terminal/orbital-density/firmware/m2_skyview/m2_skyview.ino
git commit -m "Wio GPS sky-view m2: microSD logging"
```

---

### Task 7: Documentation + push

**Files:**
- Create: `wio-terminal/orbital-density/README.md`
- Modify: `README.md` (repo root — project-table row)

- [ ] **Step 1: Write the README**

Mirror the soil monitor's README structure: intro; Hardware (Wio Terminal + Air530 on the UART Grove port, sky-view needed); wiring note (Serial1 @ 9600, which Grove port from Task 2); Firmware table (`m1_gps`, `m2_skyview` ← current); build/flash commands (real FQBN + port, "close serial monitor before upload"); a **Display** section (sky plot: rings = elevation, center = zenith, rim = horizon, N/E/S/W, dot color = constellation per the legend, dot size = SNR; header `View/Use/fix`; detail page via KEY_C); controls (KEY_C toggles page); Data section (`/gps.csv` columns, in-view vs used distinction, UTC vs uptime); a note that "in view" (GSV) ≠ "used" (in the fix).

- [ ] **Step 2: Add the root README row**

```markdown
| [wio-terminal/orbital-density](wio-terminal/orbital-density/) | Seeed Wio Terminal | active |
```

- [ ] **Step 3: Verify + push**

Cross-check README against the final sketch (pins, columns, colors, button). Then:

```bash
git add wio-terminal/orbital-density/README.md README.md
git commit -m "Add Wio GPS sky-view docs"
git push
```

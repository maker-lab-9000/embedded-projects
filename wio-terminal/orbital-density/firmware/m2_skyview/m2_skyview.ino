// GPS Sky View for the Wio Terminal + Air530 (orbital-density project) — polar
// satellite plot, detail page, and per-minute logging. NMEA from Serial1 @ 9600.
//
// WIRING: Air530 on the 40-pin HEADER UART (not a Grove port): GPS TX -> pin 10
// (BCM15/RXD), VCC -> pin 1 (3V3), GND -> pin 6.
//
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <TinyGPSPlus.h>
#include <TFT_eSPI.h>
#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"
#include <Wire.h>
#include <SparkFunBQ27441.h>   // fuel gauge on the 650 mAh battery chassis

TinyGPSPlus gps;
TFT_eSPI tft;

bool batOk = false;            // BQ27441 present (battery chassis)
bool screenOn = true;
bool prev5s = HIGH;
void setScreen(bool on);       // defined after the draw functions

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
  char buf[128]; strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  char* star = strchr(buf, '*'); if (star) *star = 0;
  // Split on commas PRESERVING empty fields (strtok would collapse the blank
  // elev/azim fields and misalign every group).
  const int MAXF = 24; char* f[MAXF]; int nf = 0;
  f[nf++] = buf;
  for (char* p = buf; *p && nf < MAXF; p++) {
    if (*p == ',') { *p = 0; f[nf++] = p + 1; }
  }
  // f[0]=$xxGSV f[1]=total f[2]=msg f[3]=inView, then groups of 4 from f[4]
  for (int i = 4; i + 3 < nf; i += 4) {
    int prn  = atoi(f[i]);
    if (prn == 0) continue;
    // elev/azim are often blank before a fix (satellite heard, position not yet
    // computed). Count it as in-view; elev=-1 marks "position unknown" so the
    // sky plot skips it while the count still includes it.
    int elev = f[i+1][0] ? atoi(f[i+1]) : -1;
    int azim = f[i+2][0] ? atoi(f[i+2]) : 0;
    int snr  = f[i+3][0] ? atoi(f[i+3]) : 0;
    upsertSat(constel, prn, elev, azim, snr);
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

// ---------- display: polar sky plot ----------

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

int countPositioned() { int n=0; for (int i=0;i<satCount;i++) if (sats[i].elev>=0) n++; return n; }

void drawHeader() {
  tft.fillRect(0, 0, 320, 20, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char h[40];
  snprintf(h, sizeof(h), "V %2d  U %2d  %s",
           countInView(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0, fixStr());
  tft.drawString(h, 4, 2);
  if (batOk) {
    int soc = lipo.soc();
    uint16_t bc = soc > 50 ? TFT_GREEN : (soc > 20 ? TFT_YELLOW : TFT_RED);
    char b[8]; snprintf(b, sizeof(b), "%3d%%", soc);
    tft.setTextColor(bc, TFT_BLACK);
    tft.drawString(b, 232, 2);
  }
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
    if (sats[i].elev < 0) continue;   // position not yet known: skip on the map
    float r = R * (90 - sats[i].elev) / 90.0f;
    float a = sats[i].azim * 0.017453292f;   // deg->rad
    int x = CX + (int)(r * sinf(a));
    int y = CY - (int)(r * cosf(a));
    int rad = sats[i].snr >= 40 ? 4 : (sats[i].snr >= 25 ? 3 : 2);
    tft.fillCircle(x, y, rad, constelColor(sats[i].constel));
  }
  // how many are heard but not yet placed (no fix / no ephemeris)
  int unp = countInView() - countPositioned();
  if (unp > 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char u[24]; snprintf(u, sizeof(u), "%d unlocated", unp);
    tft.drawString(u, 4, 228);
  }
}

// ---------- detail page ----------

int  page = 0;              // 0 = sky, 1 = detail, 2 = chart
bool prevKeyC = HIGH;

// satellite history for the chart: in-view + unlocated, over 24 h
const int      HIST_N = 288;             // 288 samples across the 288 px chart
const uint32_t HIST_PERIOD_MS = 300000;  // one sample / 5 min -> 288 = 24 h
uint8_t  viewHist[HIST_N];
uint8_t  unlocHist[HIST_N];
int      histLen = 0;
uint32_t lastHistMs = 0;

// SD logging
bool sdOk = false;
const char*    LOG_PATH = "/gps.csv";
const uint32_t LOG_PERIOD_MS = 60000;
uint32_t lastLogMs = 0;

int strongestSnr() { int m=0; for (int i=0;i<satCount;i++) if (sats[i].snr>m) m=sats[i].snr; return m; }

void drawDetail() {
  tft.fillRect(0, 20, 320, 220, TFT_BLACK);
  tft.setTextSize(2);
  char l[48]; int y = 30;
  auto row = [&](const char* s){ tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); tft.drawString(s, 8, y); y += 24; };
  snprintf(l, sizeof(l), "In view: %d  (pos %d)", countInView(), countPositioned()); row(l);
  snprintf(l, sizeof(l), "GPS %d GLO %d", countConstel(C_GPS), countConstel(C_GLO)); row(l);
  snprintf(l, sizeof(l), "GAL %d BDS %d", countConstel(C_GAL), countConstel(C_BDS)); row(l);
  snprintf(l, sizeof(l), "Used: %d  %s",
           gps.satellites.isValid()?(int)gps.satellites.value():0, fixStr()); row(l);
  snprintf(l, sizeof(l), "HDOP: %.1f  SNR %d", gps.hdop.isValid()?gps.hdop.hdop():0.0, strongestSnr()); row(l);
  if (gps.location.isValid()) snprintf(l, sizeof(l), "%.4f %.4f", gps.location.lat(), gps.location.lng());
  else snprintf(l, sizeof(l), "lat/lon: --");
  row(l);
  if (gps.altitude.isValid()) snprintf(l, sizeof(l), "Alt %.0f m", gps.altitude.meters());
  else snprintf(l, sizeof(l), "Alt: --");
  row(l);
  if (gps.time.isValid()) snprintf(l, sizeof(l), "UTC %02d:%02d:%02d",
                                   gps.time.hour(), gps.time.minute(), gps.time.second());
  else snprintf(l, sizeof(l), "UTC --");
  row(l);
}

void pollButtons() {
  bool k = digitalRead(WIO_KEY_C);
  if (prevKeyC == HIGH && k == LOW) { page = (page + 1) % 3; if (screenOn) tft.fillScreen(TFT_BLACK); }
  prevKeyC = k;

  bool s5 = digitalRead(WIO_5S_PRESS);   // 5-way center press: screen on/off
  if (prev5s == HIGH && s5 == LOW) setScreen(!screenOn);
  prev5s = s5;
}

// ---------- chart: satellites in view over time ----------

void pushHist(int v, int u) {
  if (v > 255) v = 255; if (u > 255) u = 255;
  if (histLen < HIST_N) { viewHist[histLen] = (uint8_t)v; unlocHist[histLen] = (uint8_t)u; histLen++; }
  else {
    for (int i = 1; i < HIST_N; i++) { viewHist[i-1] = viewHist[i]; unlocHist[i-1] = unlocHist[i]; }
    viewHist[HIST_N-1] = (uint8_t)v; unlocHist[HIST_N-1] = (uint8_t)u;
  }
}

void drawChart() {
  tft.fillRect(0, 20, 320, 220, TFT_BLACK);
  const int X = 22, Y = 46, W = 288, H = 158;
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);     tft.drawString("in view", X, 26);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);    tft.drawString("unlocated", X + 66, 26);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); tft.drawString("(24 h)", X + 150, 26);
  tft.drawRect(X, Y, W, H, TFT_DARKGREY);
  int mx = 12;
  for (int i = 0; i < histLen; i++) if (viewHist[i] > mx) mx = viewHist[i];
  char lbl[12]; snprintf(lbl, sizeof(lbl), "%d", mx);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(lbl, 2, Y - 3);
  tft.drawString("0", 12, Y + H - 6);
  int pvx = -1, pvy = 0, pux = -1, puy = 0;
  for (int i = 0; i < histLen; i++) {
    int x  = X + (histLen <= 1 ? 0 : (int)((long)(W - 2) * i / (histLen - 1)));
    int yv = Y + H - 1 - (int)((long)(H - 2) * viewHist[i] / mx);
    int yu = Y + H - 1 - (int)((long)(H - 2) * unlocHist[i] / mx);
    if (pvx >= 0) tft.drawLine(pvx, pvy, x, yv, TFT_GREEN);
    if (pux >= 0) tft.drawLine(pux, puy, x, yu, TFT_ORANGE);
    pvx = x; pvy = yv; pux = x; puy = yu;
  }
  // hour markers (UTC) across the 24 h window: 24h-ago, 16h, 8h, now
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };  // left->right
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) {
      char t[4]; snprintf(t, sizeof(t), "%02d", hrs[i]);
      tft.drawString(t, xs[i], Y + H + 3);
    }
  } else {
    tft.drawString("-24h", X, Y + H + 3);
    tft.drawString("now", X + W - 18, Y + H + 3);
  }
}

// ---------- SD logging ----------

void initSd() {
  sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  if (sdOk && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) { f.println("utc,uptime_s,in_view,positioned,used,fix,hdop,gps,glonass,galileo,beidou"); f.close(); }
  }
}

void drawSdBadge() {
  tft.setTextSize(2);
  tft.setTextColor(sdOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(sdOk ? "SD" : "SD!", 292, 2);
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
  f.printf("%s,%lu,%d,%d,%d,%s,%.1f,%d,%d,%d,%d\n",
           utc, (unsigned long)(millis() / 1000), countInView(), countPositioned(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
           gps.location.isValid() ? (gps.altitude.isValid() ? "3D" : "2D") : "none",
           gps.hdop.isValid() ? gps.hdop.hdop() : 0.0,
           countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS));
  f.close();
}

void drawPage() {
  drawHeader();
  drawSdBadge();
  if (page == 0) drawSky(); else if (page == 1) drawDetail(); else drawChart();
}

void setScreen(bool on) {
  screenOn = on;
  digitalWrite(LCD_BACKLIGHT, on ? HIGH : LOW);
  if (on) { tft.fillScreen(TFT_BLACK); drawPage(); }  // full redraw on wake
}

uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);

  Wire.begin();                 // fuel gauge is optional (battery chassis only)
  batOk = lipo.begin();
  if (batOk) lipo.setCapacity(650);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  initSd();
}

void loop() {
  feedGps();
  pollButtons();  // poll every loop so presses feel instant

  if (millis() - lastHistMs >= HIST_PERIOD_MS) {
    lastHistMs = millis();
    expireSats();
    pushHist(countInView(), countInView() - countPositioned());
  }

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    expireSats();
    if (screenOn) drawPage();   // backlight-off: keep parsing + logging, skip drawing
    Serial.printf("inView=%d pos=%d used=%d %s | GPS=%d GLO=%d GAL=%d BDS=%d\n",
      countInView(), countPositioned(), gps.satellites.isValid()?(int)gps.satellites.value():-1, fixStr(),
      countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS));
  }

  if (millis() - lastLogMs >= LOG_PERIOD_MS) {
    lastLogMs = millis();
    logRow();
  }
}

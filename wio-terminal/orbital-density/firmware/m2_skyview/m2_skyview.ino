// GPS Sky View for the Wio Terminal + Air530 (orbital-density project) — polar
// satellite plot, detail page, and per-minute logging. NMEA from Serial1 @ 9600.
//
// WIRING: Air530 on the 40-pin HEADER UART (not a Grove port): GPS TX -> pin 10
// (BCM15/RXD), VCC -> pin 1 (3V3), GND -> pin 6.
//
// Grove Dust Sensor (Shinyei PPD42NS) on the D0/D1 Grove port, signal -> D0.
// Grove VCC is 3.3V (sensor is rated 5V) -- readings are a relative "dust
// activity" indicator, not a calibrated concentration. Pulse width is
// measured by polling digitalRead()+micros() rather than pulseIn(): this
// core's pulseIn() does not return 0 on a clean timeout (confirmed at
// bring-up -- it returns a bogus near-ULONG_MAX value instead every time).
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
TFT_eSprite spr = TFT_eSprite(&tft);   // off-screen buffer: draw here, blit once (no flicker)

bool batOk = false;            // BQ27441 present (battery chassis)
bool screenOn = true;
bool prev5s = HIGH;
void setScreen(bool on);       // defined after the draw functions
void drawPage();
int  strongestSnr();           // defined later

// anomaly (reception-health) detection
char anomCode[12] = "OK";
bool everFixed = false;
const float HDOP_ANOM   = 8.0f;
const int   LOWSAT_ANOM = 4;
const int   LOWSNR_ANOM = 12;

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

// ---------- Mars position (Schlyter low-precision ephemeris) ----------

static double revd(double x) { x = fmod(x, 360.0); if (x < 0) x += 360.0; return x; }

// Mars altitude/azimuth in degrees for the current GPS location + UTC.
// Returns false until position and time are valid. az: 0=N, 90=E, 180=S, 270=W.
bool marsAltAz(double &altDeg, double &azDeg) {
  if (!gps.location.isValid() || !gps.date.isValid() || !gps.time.isValid()) return false;
  const double R2 = M_PI / 180.0;
  double Y = gps.date.year(), M = gps.date.month(), D = gps.date.day();
  double UT = gps.time.hour() + gps.time.minute() / 60.0 + gps.time.second() / 3600.0;
  long dn = 367L*(long)Y - 7L*((long)Y + ((long)M + 9)/12)/4 + 275L*(long)M/9 + (long)D - 730530L;
  double d = (double)dn + UT / 24.0;
  double ecl = 23.4393 - 3.563e-7 * d;

  // Sun (Earth's position + sidereal-time reference)
  double ws = 282.9404 + 4.70935e-5 * d, es = 0.016709 - 1.151e-9 * d;
  double Ms = revd(356.0470 + 0.9856002585 * d);
  double Es = Ms + (1/R2)*es*sin(Ms*R2)*(1 + es*cos(Ms*R2));
  double xvs = cos(Es*R2) - es, yvs = sqrt(1 - es*es)*sin(Es*R2);
  double vs = atan2(yvs, xvs)/R2, rs = sqrt(xvs*xvs + yvs*yvs);
  double lons = revd(vs + ws);
  double xs = rs*cos(lons*R2), ys = rs*sin(lons*R2);
  double Ls = revd(ws + Ms);

  // Mars orbital elements
  double N = 49.5574 + 2.11081e-5*d, inc = 1.8497 - 1.78e-8*d, w = 286.5016 + 2.92961e-5*d;
  double a = 1.523688, e = 0.093405 + 2.516e-9*d;
  double Mm = revd(18.6021 + 0.5240207766*d);
  double E = Mm + (1/R2)*e*sin(Mm*R2)*(1 + e*cos(Mm*R2));
  for (int it = 0; it < 4; it++) E = E - (E - (1/R2)*e*sin(E*R2) - Mm) / (1 - e*cos(E*R2));
  double xv = a*(cos(E*R2) - e), yv = a*sqrt(1 - e*e)*sin(E*R2);
  double v = atan2(yv, xv)/R2, r = sqrt(xv*xv + yv*yv);
  double xh = r*(cos(N*R2)*cos((v+w)*R2) - sin(N*R2)*sin((v+w)*R2)*cos(inc*R2));
  double yh = r*(sin(N*R2)*cos((v+w)*R2) + cos(N*R2)*sin((v+w)*R2)*cos(inc*R2));
  double zh = r*(sin((v+w)*R2)*sin(inc*R2));
  double xg = xh + xs, yg = yh + ys, zg = zh;
  double xe = xg, ye = yg*cos(ecl*R2) - zg*sin(ecl*R2), ze = yg*sin(ecl*R2) + zg*cos(ecl*R2);
  double RA = revd(atan2(ye, xe)/R2);
  double Dec = atan2(ze, sqrt(xe*xe + ye*ye))/R2;

  // topocentric alt/az
  double lat = gps.location.lat(), lon = gps.location.lng();
  double GMST0 = revd(Ls + 180.0);
  double LST = revd(GMST0 + UT*15.0 + lon);
  double HA = revd(LST - RA);
  double haR = HA*R2, decR = Dec*R2, latR = lat*R2;
  double sinAlt = sin(latR)*sin(decR) + cos(latR)*cos(decR)*cos(haR);
  double alt = asin(sinAlt)/R2;
  double cosAz = (sin(decR) - sin(latR)*sinAlt) / (cos(latR)*cos(alt*R2));
  cosAz = cosAz > 1 ? 1 : (cosAz < -1 ? -1 : cosAz);
  double az = acos(cosAz)/R2;
  if (sin(haR) > 0) az = 360.0 - az;
  altDeg = alt; azDeg = az;
  return true;
}

// ---------- anomaly (reception health) ----------

void evalAnomaly() {
  if (gps.location.isValid()) everFixed = true;
  if (!everFixed) { strcpy(anomCode, "OK"); return; }   // no working baseline yet
  const char* code = "OK";
  if (!gps.location.isValid())                                                     code = "FIX LOST";
  else if (gps.hdop.isValid() && gps.hdop.hdop() > HDOP_ANOM)                       code = "HI HDOP";
  else if ((gps.satellites.isValid() ? (int)gps.satellites.value() : 0) < LOWSAT_ANOM) code = "LOW SAT";
  else if (strongestSnr() < LOWSNR_ANOM)                                            code = "LOW SNR";
  strncpy(anomCode, code, sizeof(anomCode) - 1);
  anomCode[sizeof(anomCode) - 1] = 0;
}

bool anomalyActive() { return strcmp(anomCode, "OK") != 0; }

// ---------- dust sensor (Grove Dust Sensor / Shinyei PPD42NS) ----------

const int      DUST_PIN       = D0;
const uint32_t DUST_WINDOW_MS = 30000;   // Shinyei spec sample window

bool     dustWasLow = false;
uint32_t dustFallAtUs = 0;
uint32_t dustLowTotalUs = 0;
uint32_t dustPulses = 0;
uint32_t dustWindowStart = 0;

float dustRatio = 0;    // % low-time over the last completed window
float dustConc  = 0;    // Shinyei curve estimate, pcs/0.01cf (relative, uncalibrated at 3.3V)

const int DUST_HIST_N = 240;             // 240 samples * 30s = 2h
uint8_t   dustHist[DUST_HIST_N];         // ratio*10, clamped to fit a byte (0-25.5%)
int       dustHistLen = 0;

void pushDustHist(float ratio) {
  int v = (int)(ratio * 10.0f + 0.5f);
  if (v > 255) v = 255;
  if (dustHistLen < DUST_HIST_N) dustHist[dustHistLen++] = (uint8_t)v;
  else {
    for (int i = 1; i < DUST_HIST_N; i++) dustHist[i-1] = dustHist[i];
    dustHist[DUST_HIST_N-1] = (uint8_t)v;
  }
}

// Non-blocking pulse-width poll -- call every loop() iteration.
void pollDust() {
  bool nowLow = (digitalRead(DUST_PIN) == LOW);
  uint32_t now = micros();
  if (nowLow && !dustWasLow) {
    dustFallAtUs = now;
  } else if (!nowLow && dustWasLow) {
    dustLowTotalUs += (now - dustFallAtUs);
    dustPulses++;
  }
  dustWasLow = nowLow;

  if (millis() - dustWindowStart >= DUST_WINDOW_MS) {
    dustRatio = 100.0f * (dustLowTotalUs / 1000.0f) / DUST_WINDOW_MS;
    dustConc = 1.1f*dustRatio*dustRatio*dustRatio - 3.8f*dustRatio*dustRatio + 520.0f*dustRatio + 0.62f;
    pushDustHist(dustRatio);
    dustLowTotalUs = 0; dustPulses = 0; dustWindowStart = millis();
  }
}

void drawHeader() {
  spr.fillRect(0, 0, 320, 20, TFT_BLACK);
  spr.setTextSize(2);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  char h[40];
  snprintf(h, sizeof(h), "V %2d  U %2d  %s",
           countInView(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0, fixStr());
  spr.drawString(h, 4, 2);
  if (batOk) {
    int soc = lipo.soc();
    uint16_t bc = soc > 50 ? TFT_GREEN : (soc > 20 ? TFT_YELLOW : TFT_RED);
    char b[8]; snprintf(b, sizeof(b), "%3d%%", soc);
    spr.setTextColor(bc, TFT_BLACK);
    spr.drawString(b, 232, 2);
  }
}

void drawLegend() {
  const char* names[] = {"GPS","GLO","GAL","BDS"};
  uint8_t cs[] = {C_GPS,C_GLO,C_GAL,C_BDS};
  spr.setTextSize(1);
  int y = 26;
  for (int i = 0; i < 4; i++) {
    spr.fillRect(288, y, 8, 8, constelColor(cs[i]));
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(names[i], 300, y);
    y += 12;
  }
}

void drawSky() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  spr.drawCircle(CX, CY, R, TFT_DARKGREY);
  spr.drawCircle(CX, CY, R * 2 / 3, TFT_DARKGREY);
  spr.drawCircle(CX, CY, R / 3, TFT_DARKGREY);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("N", CX - 2, CY - R - 10);
  spr.drawString("S", CX - 2, CY + R + 2);
  spr.drawString("E", CX + R + 2, CY - 3);
  spr.drawString("W", CX - R - 10, CY - 3);
  drawLegend();
  for (int i = 0; i < satCount; i++) {
    if (sats[i].elev < 0) continue;   // position not yet known: skip on the map
    float r = R * (90 - sats[i].elev) / 90.0f;
    float a = sats[i].azim * 0.017453292f;   // deg->rad
    int x = CX + (int)(r * sinf(a));
    int y = CY - (int)(r * cosf(a));
    int rad = sats[i].snr >= 40 ? 4 : (sats[i].snr >= 25 ? 3 : 2);
    spr.fillCircle(x, y, rad, constelColor(sats[i].constel));
  }
  // Mars: plot at its real alt/az when above the horizon.
  double mAlt, mAz;
  if (marsAltAz(mAlt, mAz)) {
    if (mAlt > 0) {
      float rr = R * (90 - mAlt) / 90.0f;
      float aa = mAz * 0.017453292f;
      int mxp = CX + (int)(rr * sinf(aa));
      int myp = CY - (int)(rr * cosf(aa));
      spr.fillCircle(mxp, myp, 3, TFT_RED);
      spr.drawCircle(mxp, myp, 6, TFT_RED);
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.drawString("Mars", mxp + 9, myp - 3);
    } else {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.drawString("Mars below horizon", 190, 228);
    }
  }
  // how many are heard but not yet placed (no fix / no ephemeris)
  int unp = countInView() - countPositioned();
  if (unp > 0) {
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char u[24]; snprintf(u, sizeof(u), "%d unlocated", unp);
    spr.drawString(u, 4, 228);
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
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  spr.setTextSize(2);
  char l[48]; int y = 30;
  auto row = [&](const char* s){ spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK); spr.drawString(s, 8, y); y += 24; };
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
  if (prevKeyC == HIGH && k == LOW) { page = (page + 1) % 4; if (screenOn) drawPage(); }
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
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  const int X = 22, Y = 46, W = 288, H = 158;
  spr.setTextSize(1);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);     spr.drawString("in view", X, 26);
  spr.setTextColor(TFT_ORANGE, TFT_BLACK);    spr.drawString("unlocated", X + 66, 26);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK); spr.drawString("(24 h)", X + 150, 26);
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);
  int mx = 12;
  for (int i = 0; i < histLen; i++) if (viewHist[i] > mx) mx = viewHist[i];
  char lbl[12]; snprintf(lbl, sizeof(lbl), "%d", mx);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(lbl, 2, Y - 3);
  spr.drawString("0", 12, Y + H - 6);
  // mid-level gridline + value label, so any point's magnitude is readable
  int midv = mx / 2;
  int ymid = Y + H - 1 - (int)((long)(H - 2) * midv / mx);
  for (int gx = X + 2; gx < X + W - 2; gx += 8) spr.drawPixel(gx, ymid, TFT_DARKGREY);
  snprintf(lbl, sizeof(lbl), "%d", midv);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(lbl, 2, ymid - 3);
  // the two series
  int pvx = -1, pvy = 0, pux = -1, puy = 0;
  for (int i = 0; i < histLen; i++) {
    int x  = X + (histLen <= 1 ? 0 : (int)((long)(W - 2) * i / (histLen - 1)));
    int yv = Y + H - 1 - (int)((long)(H - 2) * viewHist[i] / mx);
    int yu = Y + H - 1 - (int)((long)(H - 2) * unlocHist[i] / mx);
    if (pvx >= 0) spr.drawLine(pvx, pvy, x, yv, TFT_GREEN);
    if (pux >= 0) spr.drawLine(pux, puy, x, yu, TFT_ORANGE);
    pvx = x; pvy = yv; pux = x; puy = yu;
  }
  // current value + peak marker with the time it occurred
  if (histLen > 0) {
    char info[28];
    snprintf(info, sizeof(info), "now %d", viewHist[histLen - 1]);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawString(info, X + W - 46, 36);
    int pk = 0;
    for (int i = 1; i < histLen; i++) if (viewHist[i] > viewHist[pk]) pk = i;
    int px = X + (histLen <= 1 ? 0 : (int)((long)(W - 2) * pk / (histLen - 1)));
    int py = Y + H - 1 - (int)((long)(H - 2) * viewHist[pk] / mx);
    spr.fillCircle(px, py, 2, TFT_WHITE);
    spr.drawCircle(px, py, 4, TFT_WHITE);
    if (gps.time.isValid()) {
      int minsAgo = ((histLen - 1) - pk) * (int)(HIST_PERIOD_MS / 60000);
      int pkMin = (((gps.time.hour() * 60 + gps.time.minute()) - minsAgo) % 1440 + 1440) % 1440;
      snprintf(info, sizeof(info), "peak %d @%02d:%02d", viewHist[pk], pkMin / 60, pkMin % 60);
    } else {
      snprintf(info, sizeof(info), "peak %d", viewHist[pk]);
    }
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString(info, X, 36);
  }
  // hour markers (UTC) across the 24 h window: 24h-ago, 16h, 8h, now
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };  // left->right
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) {
      char t[4]; snprintf(t, sizeof(t), "%02d", hrs[i]);
      spr.drawString(t, xs[i], Y + H + 3);
    }
  } else {
    spr.drawString("-24h", X, Y + H + 3);
    spr.drawString("now", X + W - 18, Y + H + 3);
  }
}

// ---------- SD logging ----------

void drawDust() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  spr.setTextSize(2);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  char l[40];
  snprintf(l, sizeof(l), "Ratio: %.2f%%   %.0f pcs", dustRatio, dustConc);
  spr.drawString(l, 8, 26);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("relative activity - uncalibrated (3.3V)", 8, 52);

  const int X = 22, Y = 82, W = 288, H = 120;
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("dust activity (2 h)", X, 68);
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);
  int mx = 10;   // in ratio*10 units, i.e. floor of 1.0%
  for (int i = 0; i < dustHistLen; i++) if (dustHist[i] > mx) mx = dustHist[i];
  char lbl[12]; snprintf(lbl, sizeof(lbl), "%.1f%%", mx / 10.0f);
  spr.drawString(lbl, 2, Y - 3);
  spr.drawString("0", 12, Y + H - 6);
  int prevX = -1, prevY = 0;
  for (int i = 0; i < dustHistLen; i++) {
    int x = X + (dustHistLen <= 1 ? 0 : (int)((long)(W - 2) * i / (dustHistLen - 1)));
    int y = Y + H - 1 - (int)((long)(H - 2) * dustHist[i] / mx);
    if (prevX >= 0) spr.drawLine(prevX, prevY, x, y, TFT_YELLOW);
    prevX = x; prevY = y;
  }
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("-2h", X, Y + H + 3);
  spr.drawString("now", X + W - 18, Y + H + 3);
}

void initSd() {
  sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  if (sdOk && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) { f.println("utc,uptime_s,in_view,positioned,used,fix,hdop,gps,glonass,galileo,beidou,anom,dust_ratio,dust_conc"); f.close(); }
  }
}

void drawSdBadge() {
  spr.setTextSize(2);
  spr.setTextColor(sdOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  spr.drawString(sdOk ? "SD" : "SD!", 292, 2);
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
  f.printf("%s,%lu,%d,%d,%d,%s,%.1f,%d,%d,%d,%d,%s,%.2f,%.1f\n",
           utc, (unsigned long)(millis() / 1000), countInView(), countPositioned(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
           gps.location.isValid() ? (gps.altitude.isValid() ? "3D" : "2D") : "none",
           gps.hdop.isValid() ? gps.hdop.hdop() : 0.0,
           countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS),
           anomCode, dustRatio, dustConc);
  f.close();
}

void drawPage() {
  spr.fillSprite(TFT_BLACK);
  drawHeader();
  drawSdBadge();
  if (page == 0) drawSky(); else if (page == 1) drawDetail(); else if (page == 2) drawChart(); else drawDust();
  if (anomalyActive()) {   // reception-health alert banner, over any page
    spr.fillRect(0, 224, 320, 16, TFT_RED);
    spr.setTextSize(2);
    spr.setTextColor(TFT_WHITE, TFT_RED);
    char m[24]; snprintf(m, sizeof(m), "! %s", anomCode);
    spr.drawString(m, 6, 225);
  }
  spr.pushSprite(0, 0);   // one atomic blit to the screen -> no flicker
}

void setScreen(bool on) {
  screenOn = on;
  digitalWrite(LCD_BACKLIGHT, on ? HIGH : LOW);
  if (on) drawPage();  // full redraw (sprite blit) on wake
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
  spr.setColorDepth(8);
  spr.createSprite(320, 240);   // ~77 KB off-screen buffer for flicker-free blits
  initSd();

  pinMode(DUST_PIN, INPUT);
  dustWindowStart = millis();
  dustWasLow = (digitalRead(DUST_PIN) == LOW);
  dustFallAtUs = micros();
}

void loop() {
  feedGps();
  pollButtons();  // poll every loop so presses feel instant
  pollDust();     // non-blocking; must run every loop iteration

  if (millis() - lastHistMs >= HIST_PERIOD_MS) {
    lastHistMs = millis();
    expireSats();
    pushHist(countInView(), countInView() - countPositioned());
  }

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    expireSats();
    evalAnomaly();
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

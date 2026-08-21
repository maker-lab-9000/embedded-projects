// GPS Sky View for the Wio Terminal + Air530 (orbital-density project) — polar
// satellite plot, detail page, and per-minute logging. NMEA from Serial1 @ 9600.
//
// WIRING: Air530 on the 40-pin HEADER UART (not a Grove port): GPS TX -> pin 10
// (BCM15/RXD), VCC -> pin 1 (3V3), GND -> pin 6.
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

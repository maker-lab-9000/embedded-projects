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
// BME280 (Seengreat) on the I2C Grove port (left side), address 0x77.
// Temp, humidity, pressure — calibrated via on-chip compensation.
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
      if (linePos > 6) {
        if (!strncmp(line + 3, "GSV", 3)) parseGsv(line);
        else if (!strncmp(line + 3, "GSA", 3)) parseGsa(line);
      }
      linePos = 0;
    } else if (c != '\r' && linePos < (int)sizeof(line) - 1) {
      line[linePos++] = c;
    }
  }
}

int countInView() { return satCount; }
int countConstel(uint8_t c) { int n = 0; for (int i=0;i<satCount;i++) if (sats[i].constel==c) n++; return n; }

// ---------- GSA parsing (per-constellation "used" counts) ----------

uint8_t gsaUsedCount[5] = {0};   // GPS, GLO, GAL, BDS, QZS
uint32_t gsaLastMs = 0;

uint8_t findConstelByPrn(int prn) {
  for (int i = 0; i < satCount; i++)
    if (sats[i].prn == prn) return sats[i].constel;
  return C_OTHER;
}

void parseGsa(const char* s) {
  if (!checksumOk(s)) return;
  char buf[128]; strncpy(buf, s, sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
  char* star = strchr(buf, '*'); if (star) *star = 0;
  const int MAXF = 20; char* f[MAXF]; int nf = 0;
  f[nf++] = buf;
  for (char* p = buf; *p && nf < MAXF; p++) {
    if (*p == ',') { *p = 0; f[nf++] = p + 1; }
  }
  if (millis() - gsaLastMs > 2000) memset(gsaUsedCount, 0, sizeof(gsaUsedCount));
  gsaLastMs = millis();

  uint8_t constel = talkerConstel(s);

  // GNGSA with system ID field (NMEA 4.10+)
  if (constel == C_OTHER && s[1] == 'G' && s[2] == 'N' && nf > 18 && f[18][0]) {
    int sysId = atoi(f[18]);
    static const uint8_t sysMap[] = {C_OTHER, C_GPS, C_GLO, C_GAL, C_BDS, C_QZS};
    if (sysId >= 1 && sysId <= 5) constel = sysMap[sysId];
  }

  // GNGSA without system ID: match each PRN against the sat table
  if (constel == C_OTHER && s[1] == 'G' && s[2] == 'N') {
    uint8_t perC[5] = {0};
    for (int i = 3; i <= 14 && i < nf; i++) {
      if (!f[i][0]) continue;
      int prn = atoi(f[i]);
      if (prn <= 0) continue;
      uint8_t c = findConstelByPrn(prn);
      if (c < 5) perC[c]++;
    }
    for (int c = 0; c < 5; c++) gsaUsedCount[c] = perC[c];
    return;
  }

  if (constel >= 5) return;
  int count = 0;
  for (int i = 3; i <= 14 && i < nf; i++)
    if (f[i][0] && atoi(f[i]) > 0) count++;
  gsaUsedCount[constel] = count;
}

int countConstelUsed(uint8_t c) { return (c < 5) ? gsaUsedCount[c] : 0; }
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

// ---------- Solar system ephemeris (Schlyter low-precision) ----------

static const double R2 = M_PI / 180.0;
static double revd(double x) { x = fmod(x, 360.0); if (x < 0) x += 360.0; return x; }

enum Body { B_SUN, B_MOON, B_MERCURY, B_VENUS, B_MARS, B_JUPITER, B_SATURN, B_COUNT };

struct BodyResult { double alt, az; bool valid; };
BodyResult bodies[B_COUNT];

static void raDecToAltAz(double RA, double Dec, double LST, double lat,
                          double &alt, double &az) {
  double HA = revd(LST - RA);
  double haR = HA*R2, decR = Dec*R2, latR = lat*R2;
  double sinAlt = sin(latR)*sin(decR) + cos(latR)*cos(decR)*cos(haR);
  alt = asin(sinAlt)/R2;
  double cosAz = (sin(decR) - sin(latR)*sinAlt) / (cos(latR)*cos(alt*R2));
  cosAz = cosAz > 1 ? 1 : (cosAz < -1 ? -1 : cosAz);
  az = acos(cosAz)/R2;
  if (sin(haR) > 0) az = 360.0 - az;
}

static void planetHelio(double d,
    double N, double inc, double w, double a, double e, double M,
    double &xh, double &yh, double &zh) {
  M = revd(M);
  double E = M + (1/R2)*e*sin(M*R2)*(1 + e*cos(M*R2));
  for (int it = 0; it < 4; it++)
    E = E - (E - (1/R2)*e*sin(E*R2) - M) / (1 - e*cos(E*R2));
  double xv = a*(cos(E*R2) - e), yv = a*sqrt(1 - e*e)*sin(E*R2);
  double v = atan2(yv, xv)/R2, r = sqrt(xv*xv + yv*yv);
  xh = r*(cos(N*R2)*cos((v+w)*R2) - sin(N*R2)*sin((v+w)*R2)*cos(inc*R2));
  yh = r*(sin(N*R2)*cos((v+w)*R2) + cos(N*R2)*sin((v+w)*R2)*cos(inc*R2));
  zh = r*(sin((v+w)*R2)*sin(inc*R2));
}

void computeBodies() {
  for (int i = 0; i < B_COUNT; i++) bodies[i].valid = false;
  if (!gps.location.isValid() || !gps.date.isValid() || !gps.time.isValid()) return;

  double Y = gps.date.year(), Mo = gps.date.month(), D = gps.date.day();
  double UT = gps.time.hour() + gps.time.minute()/60.0 + gps.time.second()/3600.0;
  long dn = 367L*(long)Y - 7L*((long)Y+((long)Mo+9)/12)/4 + 275L*(long)Mo/9 + (long)D - 730530L;
  double d = (double)dn + UT/24.0;
  double ecl = 23.4393 - 3.563e-7*d;
  double lat = gps.location.lat(), lon = gps.location.lng();

  // Sun (also gives Earth's heliocentric position for geocentric conversion)
  double ws = 282.9404 + 4.70935e-5*d, es = 0.016709 - 1.151e-9*d;
  double Ms = revd(356.0470 + 0.9856002585*d);
  double Es = Ms + (1/R2)*es*sin(Ms*R2)*(1 + es*cos(Ms*R2));
  double xvs = cos(Es*R2)-es, yvs = sqrt(1-es*es)*sin(Es*R2);
  double vs = atan2(yvs,xvs)/R2, rs = sqrt(xvs*xvs+yvs*yvs);
  double lons = revd(vs+ws);
  double xs = rs*cos(lons*R2), ys = rs*sin(lons*R2);
  double Ls = revd(ws+Ms);
  double GMST0 = revd(Ls+180.0);
  double LST = revd(GMST0 + UT*15.0 + lon);

  // Sun RA/Dec (geocentric = opposite of Earth's helio position)
  {
    double xsG = -xs, ysG = -ys;
    double ye = ysG*cos(ecl*R2), ze = ysG*sin(ecl*R2);
    double RA = revd(atan2(ye, xsG)/R2);
    double Dec = atan2(ze, sqrt(xsG*xsG+ye*ye))/R2;
    raDecToAltAz(RA, Dec, LST, lat, bodies[B_SUN].alt, bodies[B_SUN].az);
    bodies[B_SUN].valid = true;
  }

  // Moon (simplified — Schlyter)
  {
    double Nm = revd(125.1228 - 0.0529538083*d);
    double im = 5.1454;
    double wm = revd(318.0634 + 0.1643573223*d);
    double am = 60.2666; // Earth radii
    double em = 0.054900;
    double Mm = revd(115.3654 + 13.0649929509*d);
    double Em = Mm + (1/R2)*em*sin(Mm*R2)*(1+em*cos(Mm*R2));
    for (int it=0;it<4;it++)
      Em = Em-(Em-(1/R2)*em*sin(Em*R2)-Mm)/(1-em*cos(Em*R2));
    double xv = am*(cos(Em*R2)-em), yv = am*sqrt(1-em*em)*sin(Em*R2);
    double vm = atan2(yv,xv)/R2, rm = sqrt(xv*xv+yv*yv);
    double xh = rm*(cos(Nm*R2)*cos((vm+wm)*R2) - sin(Nm*R2)*sin((vm+wm)*R2)*cos(im*R2));
    double yh = rm*(sin(Nm*R2)*cos((vm+wm)*R2) + cos(Nm*R2)*sin((vm+wm)*R2)*cos(im*R2));
    double zh = rm*(sin((vm+wm)*R2)*sin(im*R2));
    // perturbations
    double Lm = revd(Nm+wm+Mm), Df = revd(Lm-Ls), F = revd(Lm-Nm);
    double dlon = -1.274*sin((Mm-2*Df)*R2) + 0.658*sin(2*Df*R2)
                  -0.186*sin(Ms*R2) - 0.059*sin((2*Mm-2*Df)*R2)
                  -0.057*sin((Mm-2*Df+Ms)*R2) + 0.053*sin((Mm+2*Df)*R2)
                  +0.046*sin((2*Df-Ms)*R2) + 0.041*sin((Mm-Ms)*R2)
                  -0.035*sin(Df*R2) - 0.031*sin((Mm+Ms)*R2)
                  -0.015*sin((2*F-2*Df)*R2) + 0.011*sin((Mm-4*Df)*R2);
    double dlat = -0.173*sin((F-2*Df)*R2) - 0.055*sin((Mm-F-2*Df)*R2)
                  -0.046*sin((Mm+F-2*Df)*R2) + 0.033*sin((F+2*Df)*R2)
                  +0.017*sin((2*Mm+F)*R2);
    double drad = -0.58*cos((Mm-2*Df)*R2) - 0.46*cos(2*Df*R2);
    double lonEcl = atan2(yh,xh)/R2 + dlon;
    double latEcl = atan2(zh,sqrt(xh*xh+yh*yh))/R2 + dlat;
    rm = rm + drad;
    double xec = rm*cos(latEcl*R2)*cos(lonEcl*R2);
    double yec = rm*cos(latEcl*R2)*sin(lonEcl*R2);
    double zec = rm*sin(latEcl*R2);
    double xe = xec, ye2 = yec*cos(ecl*R2)-zec*sin(ecl*R2), ze2 = yec*sin(ecl*R2)+zec*cos(ecl*R2);
    double RA = revd(atan2(ye2,xe)/R2);
    double Dec = atan2(ze2,sqrt(xe*xe+ye2*ye2))/R2;
    raDecToAltAz(RA, Dec, LST, lat, bodies[B_MOON].alt, bodies[B_MOON].az);
    bodies[B_MOON].valid = true;
  }

  // Planets — orbital elements at epoch d, geocentric via Earth offset
  struct PlanetElems { int body; double N0,N1, i0,i1, w0,w1, a, e0,e1, M0,M1; };
  static const PlanetElems elems[] = {
    { B_MERCURY,  48.3313,3.24587e-5,  7.0047,5.00e-8,  29.1241,1.01444e-5,
      0.387098, 0.205635,5.59e-10,  168.6562,4.0923344368 },
    { B_VENUS, 76.6799,2.46590e-5, 3.3946,2.75e-8, 54.8910,1.38374e-5,
      0.723330, 0.006773,-1.302e-9, 48.0052,1.6021302244 },
    { B_MARS, 49.5574,2.11081e-5, 1.8497,-1.78e-8, 286.5016,2.92961e-5,
      1.523688, 0.093405,2.516e-9, 18.6021,0.5240207766 },
    { B_JUPITER, 100.4542,2.76854e-5, 1.3030,-1.557e-7, 273.8777,1.64505e-5,
      5.20256, 0.048498,4.469e-9, 19.8950,0.0830853001 },
    { B_SATURN, 113.6634,2.38980e-5, 2.4886,-1.081e-7, 339.3939,2.97661e-5,
      9.55475, 0.055546,-9.499e-9, 316.9670,0.0334442282 },
  };

  for (int p = 0; p < 5; p++) {
    const PlanetElems &el = elems[p];
    double N = el.N0+el.N1*d, inc = el.i0+el.i1*d, w = el.w0+el.w1*d;
    double e = el.e0+el.e1*d, M = el.M0+el.M1*d;
    double xh,yh,zh;
    planetHelio(d, N,inc,w,el.a,e,M, xh,yh,zh);

    // Jupiter-Saturn mutual perturbation
    if (el.body == B_JUPITER || el.body == B_SATURN) {
      double Mj = revd(19.8950+0.0830853001*d);
      double Ms2 = revd(316.9670+0.0334442282*d);
      if (el.body == B_JUPITER) {
        double dlon = -0.332*sin((2*Mj-5*Ms2-67.6)*R2)
                      -0.056*sin((2*Mj-2*Ms2+21)*R2)
                      +0.042*sin((3*Mj-5*Ms2+21)*R2)
                      -0.036*sin((Mj-2*Ms2)*R2)
                      +0.022*cos((Mj-Ms2)*R2)
                      +0.023*sin((2*Mj-3*Ms2+52)*R2);
        double r0 = sqrt(xh*xh+yh*yh+zh*zh);
        double lonH = atan2(yh,xh)/R2 + dlon;
        double latH = atan2(zh,sqrt(xh*xh+yh*yh))/R2;
        xh = r0*cos(latH*R2)*cos(lonH*R2);
        yh = r0*cos(latH*R2)*sin(lonH*R2);
        zh = r0*sin(latH*R2);
      } else {
        double dlon = +0.812*sin((2*Mj-5*Ms2-67.6)*R2)
                      -0.229*cos((2*Mj-4*Ms2-2)*R2)
                      +0.119*sin((Mj-2*Ms2-3)*R2)
                      +0.046*sin((2*Mj-6*Ms2-69)*R2)
                      +0.014*sin((Mj-3*Ms2+32)*R2);
        double dlat = -0.020*cos((2*Mj-4*Ms2-2)*R2)
                      +0.018*sin((2*Mj-6*Ms2-49)*R2);
        double r0 = sqrt(xh*xh+yh*yh+zh*zh);
        double lonH = atan2(yh,xh)/R2 + dlon;
        double latH = atan2(zh,sqrt(xh*xh+yh*yh))/R2 + dlat;
        xh = r0*cos(latH*R2)*cos(lonH*R2);
        yh = r0*cos(latH*R2)*sin(lonH*R2);
        zh = r0*sin(latH*R2);
      }
    }

    double xg = xh+xs, yg = yh+ys, zg = zh;
    double xe = xg, ye2 = yg*cos(ecl*R2)-zg*sin(ecl*R2), ze2 = yg*sin(ecl*R2)+zg*cos(ecl*R2);
    double RA = revd(atan2(ye2,xe)/R2);
    double Dec = atan2(ze2,sqrt(xe*xe+ye2*ye2))/R2;
    raDecToAltAz(RA, Dec, LST, lat, bodies[el.body].alt, bodies[el.body].az);
    bodies[el.body].valid = true;
  }
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

const int DUST_HIST_N = 288;             // 288 samples * 5min = 24h
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
    dustLowTotalUs = 0; dustPulses = 0; dustWindowStart = millis();
  }
}

// ---------- BME280 (I2C Grove port, address 0x77) ----------

const uint8_t BME_ADDR      = 0x77;
const uint8_t BME_REG_ID    = 0xD0;
const uint8_t BME_REG_HUM   = 0xF2;
const uint8_t BME_REG_MEAS  = 0xF4;
const uint8_t BME_REG_CFG   = 0xF5;
const uint8_t BME_REG_CAL0  = 0x88;
const uint8_t BME_REG_CAL26 = 0xE1;
const uint8_t BME_REG_DATA  = 0xF7;

bool bmeOk = false;
uint16_t bT1; int16_t bT2, bT3;
uint16_t bP1; int16_t bP2,bP3,bP4,bP5,bP6,bP7,bP8,bP9;
uint8_t  bH1; int16_t bH2; uint8_t bH3; int16_t bH4,bH5; int8_t bH6;
int32_t  bme_t_fine;

float bmeTemp = 0, bmeHum = 0, bmePres = 0;

uint8_t bmeReadReg(uint8_t reg) {
  Wire.beginTransmission(BME_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BME_ADDR, (uint8_t)1);
  return Wire.read();
}

void bmeReadRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(BME_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BME_ADDR, len);
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
}

void bmeReadCalibration() {
  uint8_t c[26];
  bmeReadRegs(BME_REG_CAL0, c, 26);
  bT1 = c[0]|(c[1]<<8); bT2 = c[2]|(c[3]<<8); bT3 = c[4]|(c[5]<<8);
  bP1 = c[6]|(c[7]<<8); bP2 = c[8]|(c[9]<<8); bP3 = c[10]|(c[11]<<8);
  bP4 = c[12]|(c[13]<<8); bP5 = c[14]|(c[15]<<8); bP6 = c[16]|(c[17]<<8);
  bP7 = c[18]|(c[19]<<8); bP8 = c[20]|(c[21]<<8); bP9 = c[22]|(c[23]<<8);
  bH1 = c[25];
  uint8_t h[7];
  bmeReadRegs(BME_REG_CAL26, h, 7);
  bH2 = h[0]|(h[1]<<8); bH3 = h[2];
  bH4 = (h[3]<<4)|(h[4]&0x0F); bH5 = (h[5]<<4)|(h[4]>>4); bH6 = h[6];
}

float bmeCompTemp(int32_t adc) {
  int32_t v1 = ((((adc>>3)-((int32_t)bT1<<1)))*(int32_t)bT2)>>11;
  int32_t v2 = (((((adc>>4)-(int32_t)bT1)*((adc>>4)-(int32_t)bT1))>>12)*(int32_t)bT3)>>14;
  bme_t_fine = v1+v2;
  return (bme_t_fine*5+128)/256/100.0f;
}

float bmeCompPres(int32_t adc) {
  int64_t v1 = (int64_t)bme_t_fine - 128000;
  int64_t v2 = v1*v1*(int64_t)bP6 + ((v1*(int64_t)bP5)<<17) + ((int64_t)bP4<<35);
  v1 = ((v1*v1*(int64_t)bP3)>>8) + ((v1*(int64_t)bP2)<<12);
  v1 = (((int64_t)1<<47)+v1)*(int64_t)bP1>>33;
  if (v1==0) return 0;
  int64_t p = 1048576-adc;
  p = (((p<<31)-v2)*3125)/v1;
  v1 = ((int64_t)bP9*(p>>13)*(p>>13))>>25;
  v2 = ((int64_t)bP8*p)>>19;
  return ((p+v1+v2)>>8)/256.0f/100.0f;
}

float bmeCompHum(int32_t adc) {
  int32_t v = bme_t_fine - 76800;
  v = (((adc<<14)-((int32_t)bH4<<20)-((int32_t)bH5*v))+16384)>>15;
  v = v*(((((((v*(int32_t)bH6)>>10)*(((v*(int32_t)bH3)>>11)+32768))>>10)+2097152)*(int32_t)bH2+8192)>>14);
  v = v-(((((v>>15)*(v>>15))>>7)*(int32_t)bH1)>>4);
  if (v<0) v=0; if (v>419430400) v=419430400;
  return (v>>12)/1024.0f;
}

bool bmeInit() {
  uint8_t id = bmeReadReg(BME_REG_ID);
  if (id != 0x60 && id != 0x58) return false;
  bmeReadCalibration();
  Wire.beginTransmission(BME_ADDR); Wire.write(BME_REG_HUM); Wire.write(0x01); Wire.endTransmission();
  Wire.beginTransmission(BME_ADDR); Wire.write(BME_REG_MEAS); Wire.write(0x27); Wire.endTransmission();
  Wire.beginTransmission(BME_ADDR); Wire.write(BME_REG_CFG); Wire.write(0xA0); Wire.endTransmission();
  return true;
}

void bmeRead() {
  uint8_t buf[8];
  bmeReadRegs(BME_REG_DATA, buf, 8);
  int32_t pRaw = ((int32_t)buf[0]<<12)|((int32_t)buf[1]<<4)|(buf[2]>>4);
  int32_t tRaw = ((int32_t)buf[3]<<12)|((int32_t)buf[4]<<4)|(buf[5]>>4);
  int32_t hRaw = ((int32_t)buf[6]<<8)|buf[7];
  bmeTemp = bmeCompTemp(tRaw);
  bmePres = bmeCompPres(pRaw);
  bmeHum  = bmeCompHum(hRaw);
}

// 24h rolling history for the env chart (sampled every 5 min, same as sat chart)
const int      ENV_HIST_N = 288;
int16_t  tempHist[ENV_HIST_N];     // temp * 10
uint8_t  humHist[ENV_HIST_N];      // humidity rounded
uint16_t presHist[ENV_HIST_N];     // pressure * 10 (e.g. 9984 = 998.4 hPa)
int      envHistLen = 0;

void pushEnvHist() {
  int t = (int)(bmeTemp * 10.0f);
  int h = (int)(bmeHum + 0.5f);
  int p = (int)(bmePres * 10.0f + 0.5f);
  if (h > 255) h = 255; if (h < 0) h = 0;
  if (p > 65535) p = 65535; if (p < 0) p = 0;
  if (envHistLen < ENV_HIST_N) {
    tempHist[envHistLen] = (int16_t)t;
    humHist[envHistLen] = (uint8_t)h;
    presHist[envHistLen] = (uint16_t)p;
    envHistLen++;
  } else {
    for (int i = 1; i < ENV_HIST_N; i++) { tempHist[i-1] = tempHist[i]; humHist[i-1] = humHist[i]; presHist[i-1] = presHist[i]; }
    tempHist[ENV_HIST_N-1] = (int16_t)t;
    humHist[ENV_HIST_N-1] = (uint8_t)h;
    presHist[ENV_HIST_N-1] = (uint16_t)p;
  }
}

// ---------- weather forecast (pressure-trend + humidity heuristic) ----------

enum Weather { W_WAIT, W_STORM, W_RAIN, W_CHANGE, W_FAIR, W_STABLE };
Weather wxState = W_WAIT;
float wxDeltaP = 0;   // 3h pressure change in hPa

void evalWeather() {
  // Need at least 36 samples (36 * 5 min = 3 h) of pressure history
  if (envHistLen < 36) { wxState = W_WAIT; return; }
  uint16_t pNow = presHist[envHistLen - 1];
  uint16_t p3h  = presHist[envHistLen - 36];
  wxDeltaP = (pNow - p3h) / 10.0f;  // hPa change over 3h
  uint8_t hNow = humHist[envHistLen - 1];

  if (wxDeltaP <= -3.0f) {
    wxState = (hNow >= 80) ? W_STORM : W_RAIN;
  } else if (wxDeltaP <= -1.5f) {
    wxState = (hNow >= 80) ? W_RAIN : W_CHANGE;
  } else if (wxDeltaP >= 1.5f) {
    wxState = W_FAIR;
  } else {
    wxState = W_STABLE;
  }
}

const char* wxLabel() {
  switch (wxState) {
    case W_STORM:  return "STORM LIKELY";
    case W_RAIN:   return "RAIN POSSIBLE";
    case W_CHANGE: return "CHANGE";
    case W_FAIR:   return "FAIR";
    case W_STABLE: return "STABLE";
    default:       return "WAIT";
  }
}

uint16_t wxColor() {
  switch (wxState) {
    case W_STORM:  return TFT_RED;
    case W_RAIN:   return TFT_YELLOW;
    case W_CHANGE: return TFT_ORANGE;
    case W_FAIR:   return TFT_GREEN;
    case W_STABLE: return TFT_LIGHTGREY;
    default:       return TFT_DARKGREY;
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
  // Solar system bodies
  static const char* bodyName[] = {"Sun","Moon","Mer","Ven","Mars","Jup","Sat"};
  static const uint16_t bodyColor[] = {
    TFT_YELLOW, 0xFFFF, 0xAD55, 0xB7FF, TFT_RED, 0xFD20, 0xBDAD
  };
  int belowCount = 0;
  for (int b = 0; b < B_COUNT; b++) {
    if (!bodies[b].valid) continue;
    uint16_t col = bodyColor[b];
    if (bodies[b].alt > 0) {
      float rr = R * (90 - bodies[b].alt) / 90.0f;
      float aa = bodies[b].az * 0.017453292f;
      int bx = CX + (int)(rr * sinf(aa));
      int by = CY - (int)(rr * cosf(aa));
      if (b == B_SUN) {
        spr.fillCircle(bx, by, 5, col);
        spr.drawCircle(bx, by, 7, col);
      } else if (b == B_MOON) {
        spr.fillCircle(bx, by, 4, col);
        spr.drawCircle(bx, by, 6, TFT_DARKGREY);
      } else {
        spr.fillCircle(bx, by, 3, col);
        spr.drawCircle(bx, by, 6, col);
      }
      spr.setTextColor(col, TFT_BLACK);
      spr.drawString(bodyName[b], bx + 9, by - 3);
    } else {
      belowCount++;
    }
  }
  if (belowCount > 0 && belowCount < B_COUNT) {
    char below[64] = ""; int pos = 0;
    for (int b = 0; b < B_COUNT; b++) {
      if (!bodies[b].valid || bodies[b].alt > 0) continue;
      if (pos > 0) { below[pos++] = ' '; }
      int len = strlen(bodyName[b]);
      memcpy(below+pos, bodyName[b], len); pos += len;
    }
    below[pos] = 0;
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString(below, 190, 228);
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
  if (prevKeyC == HIGH && k == LOW) { page = (page + 1) % 6; if (screenOn) drawPage(); }
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
  spr.drawString("dust activity (24 h)", X, 68);
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
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };
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

void drawEnv() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  spr.setTextSize(2);
  if (!bmeOk) {
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.drawString("BME280 not found", 8, 26);
    return;
  }
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  char l[40];
  snprintf(l, sizeof(l), "%.1f C", bmeTemp);
  spr.drawString(l, 8, 26);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(l, sizeof(l), "%.0f %%RH", bmeHum);
  spr.drawString(l, 140, 26);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  snprintf(l, sizeof(l), "%.1f hPa", bmePres);
  spr.drawString(l, 8, 50);
  // weather forecast
  spr.setTextColor(wxColor(), TFT_BLACK);
  if (wxState != W_WAIT) {
    char wx[32];
    snprintf(wx, sizeof(wx), "%s (%.1f)", wxLabel(), wxDeltaP);
    spr.drawString(wx, 160, 50);
  } else {
    spr.setTextSize(1);
    spr.drawString("forecast in ~3h", 170, 54);
    spr.setTextSize(2);
  }

  // 24h chart: temp (cyan) + humidity (green) + pressure (yellow)
  const int X = 22, Y = 82, W = 288, H = 120;
  spr.setTextSize(1);
  spr.setTextColor(TFT_CYAN, TFT_BLACK);      spr.drawString("temp", X, 68);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);     spr.drawString("hum", X + 34, 68);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);    spr.drawString("pres", X + 60, 68);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK); spr.drawString("(24 h)", X + 96, 68);
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) {
      char t[4]; snprintf(t, sizeof(t), "%02d", hrs[i]);
      spr.drawString(t, xs[i], Y+H+3);
    }
  } else {
    spr.drawString("-24h", X, Y+H+3);
    spr.drawString("now", X+W-18, Y+H+3);
  }

  if (envHistLen < 2) return;

  // find temp range
  int16_t tMin = tempHist[0], tMax = tempHist[0];
  for (int i = 1; i < envHistLen; i++) {
    if (tempHist[i] < tMin) tMin = tempHist[i];
    if (tempHist[i] > tMax) tMax = tempHist[i];
  }
  if (tMax - tMin < 20) { int16_t mid = (tMax+tMin)/2; tMin = mid-10; tMax = mid+10; }

  // find pressure range
  uint16_t pMin = presHist[0], pMax = presHist[0];
  for (int i = 1; i < envHistLen; i++) {
    if (presHist[i] < pMin) pMin = presHist[i];
    if (presHist[i] > pMax) pMax = presHist[i];
  }
  if (pMax - pMin < 20) { uint16_t mid = (pMax+pMin)/2; pMin = mid > 10 ? mid-10 : 0; pMax = mid+10; }

  // temp axis labels (left)
  snprintf(l, sizeof(l), "%.0f", tMax/10.0f);
  spr.setTextColor(TFT_CYAN, TFT_BLACK); spr.drawString(l, 2, Y-3);
  snprintf(l, sizeof(l), "%.0f", tMin/10.0f);
  spr.drawString(l, 2, Y+H-6);

  // pressure axis labels (right)
  snprintf(l, sizeof(l), "%.0f", pMax/10.0f);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK); spr.drawString(l, X+W+2, Y-3);
  snprintf(l, sizeof(l), "%.0f", pMin/10.0f);
  spr.drawString(l, X+W+2, Y+H-6);

  // plot all three lines
  int ptx=-1, pty=0, phx=-1, phy=0, ppx=-1, ppy=0;
  for (int i = 0; i < envHistLen; i++) {
    int x = X + (int)((long)(W-2)*i/(envHistLen-1));
    int yt = Y+H-1 - (int)((long)(H-2)*(tempHist[i]-tMin)/(tMax-tMin));
    int yh = Y+H-1 - (int)((long)(H-2)*humHist[i]/100);
    int yp = Y+H-1 - (pMax==pMin ? (H-2)/2 : (int)((long)(H-2)*(presHist[i]-pMin)/(pMax-pMin)));
    if (ptx >= 0) spr.drawLine(ptx, pty, x, yt, TFT_CYAN);
    if (phx >= 0) spr.drawLine(phx, phy, x, yh, TFT_GREEN);
    if (ppx >= 0) spr.drawLine(ppx, ppy, x, yp, TFT_YELLOW);
    ptx=x; pty=yt; phx=x; phy=yh; ppx=x; ppy=yp;
  }
}

// ---------- 24h constellation observation table ----------

uint8_t cVisHist[5][288];
uint8_t cUsedHist[5][288];
int cHistLen = 0;

void pushConstelHist() {
  for (int c = 0; c < 5; c++) {
    uint8_t vis = countConstel(c);
    uint8_t used = countConstelUsed(c);
    if (cHistLen < 288) {
      cVisHist[c][cHistLen] = vis;
      cUsedHist[c][cHistLen] = used;
    } else {
      memmove(cVisHist[c], cVisHist[c]+1, 287);
      memmove(cUsedHist[c], cUsedHist[c]+1, 287);
      cVisHist[c][287] = vis;
      cUsedHist[c][287] = used;
    }
  }
  if (cHistLen < 288) cHistLen++;
}

void drawObs() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  spr.setTextSize(2);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("24H OBSERVATION", 4, 24);

  static const char* cName[] = {"USA GPS","Russia GLO","EU Galileo","China BDS","Japan QZS"};
  static const uint8_t cMap[] = {C_GPS, C_GLO, C_GAL, C_BDS, C_QZS};
  static const uint16_t cCol[] = {TFT_GREEN, TFT_CYAN, TFT_ORANGE, TFT_MAGENTA, TFT_YELLOW};

  struct CS { int idx; float avgVis, avgUsed; uint8_t peak; };
  CS st[5];
  for (int i = 0; i < 5; i++) {
    st[i].idx = i;
    int sv = 0, su = 0, pk = 0;
    for (int j = 0; j < cHistLen; j++) {
      sv += cVisHist[cMap[i]][j];
      su += cUsedHist[cMap[i]][j];
      if (cVisHist[cMap[i]][j] > pk) pk = cVisHist[cMap[i]][j];
    }
    if (cHistLen > 0) {
      st[i].avgVis = (float)sv / cHistLen;
      st[i].avgUsed = (float)su / cHistLen;
      st[i].peak = pk;
    } else {
      st[i].avgVis = countConstel(cMap[i]);
      st[i].avgUsed = countConstelUsed(cMap[i]);
      st[i].peak = countConstel(cMap[i]);
    }
  }
  for (int i = 0; i < 4; i++)
    for (int j = i+1; j < 5; j++)
      if (st[j].avgVis > st[i].avgVis) { CS t = st[i]; st[i] = st[j]; st[j] = t; }

  auto rDraw = [](int sz, const char* s, int rx, int y) {
    int w = strlen(s) * 6 * sz;
    spr.drawString(s, rx - w, y);
  };

  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  rDraw(1, "Visible", 198, 50);
  rDraw(1, "Peak", 248, 50);
  rDraw(1, "Used", 308, 50);

  int y = 66;
  for (int i = 0; i < 5; i++) {
    int ci = st[i].idx;
    spr.setTextSize(2);
    spr.setTextColor(cCol[ci], TFT_BLACK);
    spr.drawString(cName[ci], 4, y);
    char v[8];
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(v, sizeof(v), "%.1f", st[i].avgVis);
    rDraw(2, v, 198, y);
    snprintf(v, sizeof(v), "%d", st[i].peak);
    rDraw(2, v, 248, y);
    snprintf(v, sizeof(v), "%.1f", st[i].avgUsed);
    rDraw(2, v, 308, y);
    y += 26;
  }

  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  float hours = cHistLen * 5.0f / 60.0f;
  char info[32];
  if (hours >= 24.0f) snprintf(info, sizeof(info), "24h (full)");
  else snprintf(info, sizeof(info), "%.1fh of data", hours);
  spr.drawString(info, 4, 228);
}

void initSd() {
  sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  if (sdOk && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) { f.println("utc,uptime_s,in_view,positioned,used,fix,hdop,gps,glonass,galileo,beidou,anom,dust_ratio,dust_conc,temp_c,humidity,pressure_hpa,weather"); f.close(); }
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
  f.printf("%s,%lu,%d,%d,%d,%s,%.1f,%d,%d,%d,%d,%s,%.2f,%.1f,%.2f,%.1f,%.1f,%s\n",
           utc, (unsigned long)(millis() / 1000), countInView(), countPositioned(),
           gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
           gps.location.isValid() ? (gps.altitude.isValid() ? "3D" : "2D") : "none",
           gps.hdop.isValid() ? gps.hdop.hdop() : 0.0,
           countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS),
           anomCode, dustRatio, dustConc, bmeTemp, bmeHum, bmePres, wxLabel());
  f.close();
}

void drawPage() {
  spr.fillSprite(TFT_BLACK);
  drawHeader();
  drawSdBadge();
  if (page == 0) drawSky(); else if (page == 1) drawDetail(); else if (page == 2) drawChart(); else if (page == 3) drawDust(); else if (page == 4) drawEnv(); else drawObs();
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
  delay(200);
  Serial1.println("$PGKC115,1,1,1,1*2A");

  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);

  Wire.begin();                 // fuel gauge is optional (battery chassis only)
  batOk = lipo.begin();
  if (batOk) lipo.setCapacity(650);
  bmeOk = bmeInit();
  if (bmeOk) bmeRead();

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
    pushDustHist(dustRatio);
    pushConstelHist();
    if (bmeOk) { pushEnvHist(); evalWeather(); }
  }

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    expireSats();
    evalAnomaly();
    computeBodies();
    if (bmeOk) bmeRead();
    if (screenOn) drawPage();   // backlight-off: keep parsing + logging, skip drawing
    Serial.printf("inView=%d pos=%d used=%d %s | GPS=%d GLO=%d GAL=%d BDS=%d | T=%.1f H=%.0f P=%.0f\n",
      countInView(), countPositioned(), gps.satellites.isValid()?(int)gps.satellites.value():-1, fixStr(),
      countConstel(C_GPS), countConstel(C_GLO), countConstel(C_GAL), countConstel(C_BDS),
      bmeTemp, bmeHum, bmePres);
  }

  if (millis() - lastLogMs >= LOG_PERIOD_MS) {
    lastLogMs = millis();
    logRow();
  }
}

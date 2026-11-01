// Task 4 (temporary): HA session + serial dump of TramState every 10 s.
// Serial 'r' forces a websocket reconnect.
#include "net.h"
#include "ha.h"

static TramState tram{};
static uint32_t lastDump = 0;

static void dump() {
  char b[24];
  uint32_t now = nowUtc();
  Serial.print("-- st="); Serial.print(haStatus());
  formatClock(now, b, sizeof(b)); Serial.print(" clock="); Serial.print(b);
  Serial.print(" deps="); Serial.print(tram.depKnown ? tram.depCount : -1);
  Serial.print(" gap="); if (tram.gapKnown) Serial.print(tram.gapMin); else Serial.print("?");
  formatAgo(now, tram.lastDeparted, b, sizeof(b)); Serial.print(" last="); Serial.print(b);
  Serial.print(" hist="); Serial.println(tram.histKnown ? tram.histCount : -1);
  for (uint8_t i = 0; i < tram.depCount; i++) {
    formatCountdown(now, tram.dep[i].epoch, b, sizeof(b));
    Serial.print("   "); Serial.print(tram.dep[i].line); Serial.print(" "); Serial.print(tram.dep[i].time);
    Serial.print(" "); Serial.print(b); Serial.print(" +"); Serial.print(tram.dep[i].delayMin);
    Serial.print(" "); Serial.println(tram.dep[i].dir);
  }
  if (tram.histCount) {
    Serial.print("   hist last t="); Serial.print(tram.hist[tram.histCount - 1].t);
    Serial.print(" v="); Serial.println(tram.hist[tram.histCount - 1].v);
  }
  Serial.print("   ws dropped="); Serial.println(haWs.dropped());
  tram.dirty = 0;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 4000) {}
  Serial.println("\n== m1_tram (task 4 dump) ==");
  netInit();
  haInit(&tram);
}

void loop() {
  netPoll();
  haPoll();
  if (Serial.available() && Serial.read() == 'r') haForceReconnect();
  if (millis() - lastDump > 10000) { lastDump = millis(); dump(); }
}

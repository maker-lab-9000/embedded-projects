// Diagnostic — 40-pin header activity sweep. Finds which Wio Terminal header GPIO a signal
// (e.g. the GPS TX line behind a battery-chassis Grove socket) is actually toggling on.
// Every listed pin is set to INPUT (no pull-up) and its edges are counted for 1 s, every 3 s.
// A UART TX line carrying NMEA shows hundreds to thousands of edges per second in 1 Hz
// bursts; idle lines show 0. Run it with nothing else driving these pins.
// FQBN: Seeeduino:samd:seeed_wio_terminal

struct P { uint8_t pin; uint8_t bcm; uint8_t hdr; };   // Arduino pin, Raspberry-Pi BCM number, physical header pin
const P PINS[] = {
  {BCM2,2,3},{BCM3,3,5},{BCM4,4,7},{BCM14,14,8},{BCM15,15,10},{BCM17,17,11},{BCM18,18,12},
  {BCM27,27,13},{BCM22,22,15},{BCM23,23,16},{BCM24,24,18},{BCM10,10,19},{BCM9,9,21},
  {BCM25,25,22},{BCM11,11,23},{BCM8,8,24},{BCM7,7,26},{BCM0,0,27},{BCM1,1,28},{BCM5,5,29},
  {BCM6,6,31},{BCM12,12,32},{BCM13,13,33},{BCM19,19,35},{BCM16,16,36},{BCM26,26,37},
  {BCM20,20,38},{BCM21,21,40},
};
const int N = sizeof(PINS) / sizeof(PINS[0]);
uint32_t edges[N];
bool     last[N];

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  for (int i = 0; i < N; i++) { pinMode(PINS[i].pin, INPUT); last[i] = digitalRead(PINS[i].pin); }
}

void loop() {
  memset(edges, 0, sizeof(edges));
  uint32_t t0 = millis(), passes = 0;
  while (millis() - t0 < 1000) {
    for (int i = 0; i < N; i++) {
      bool v = digitalRead(PINS[i].pin);
      if (v != last[i]) { edges[i]++; last[i] = v; }
    }
    passes++;
  }
  Serial.printf("--- 1 s sweep, %lu passes ---\n", (unsigned long)passes);
  bool any = false;
  for (int i = 0; i < N; i++) {
    if (!edges[i]) continue;
    any = true;
    Serial.printf("  header pin %2u (BCM%-2u, Arduino %2u): %lu edges, now %s\n",
                  PINS[i].hdr, PINS[i].bcm, PINS[i].pin, (unsigned long)edges[i], last[i] ? "HIGH" : "LOW");
  }
  if (!any) Serial.println("  no toggling pin: is the GPS powered (its LED)? is the cable seated?");
  delay(2000);
}

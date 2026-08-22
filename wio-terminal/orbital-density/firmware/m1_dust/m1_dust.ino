// Milestone 1 — Grove Dust Sensor (Shinyei PPD42NS) bring-up.
// Digital pulse output: the pin goes LOW while a dust particle is in the
// beam. We sum that LOW time (LPO - Low Pulse Occupancy) over a sample
// window and print raw + derived numbers over serial.
//
// NOTE: pulseIn() on this core does not return 0 on a clean timeout (it
// returns a bogus near-ULONG_MAX value instead) -- confirmed by a reject
// count matching the 1 Hz timeout cadence almost exactly. Polling the pin
// directly with micros() sidesteps that entirely.
//
// Wiring: dust sensor signal (yellow) -> Wio D0/D1 Grove port, pin D0.
// FQBN: Seeeduino:samd:seeed_wio_terminal

const int PIN = D0;
const unsigned long SAMPLE_MS = 30000;  // Shinyei spec sample window

unsigned long lowTotalUs = 0;
unsigned long windowStart = 0;
unsigned long pulses = 0;

bool wasLow = false;
unsigned long fallAtUs = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN, INPUT);
  windowStart = millis();
  wasLow = (digitalRead(PIN) == LOW);
  fallAtUs = micros();
}

void loop() {
  bool nowLow = (digitalRead(PIN) == LOW);
  unsigned long now = micros();

  if (nowLow && !wasLow) {
    fallAtUs = now;                       // falling edge: pulse starts
  } else if (!nowLow && wasLow) {
    unsigned long dur = now - fallAtUs;   // rising edge: pulse ends
    lowTotalUs += dur;
    pulses++;
  }
  wasLow = nowLow;

  if (millis() - windowStart >= SAMPLE_MS) {
    float ratio = 100.0f * (lowTotalUs / 1000.0f) / SAMPLE_MS;  // % low time
    float conc = 1.1f*ratio*ratio*ratio - 3.8f*ratio*ratio + 520.0f*ratio + 0.62f;  // pcs/0.01cf (Shinyei curve, 30s cal.)
    Serial.printf("window=%lums pulses=%lu lowTotal=%lums ratio=%.3f%% concEst=%.1f\n",
                  SAMPLE_MS, pulses, lowTotalUs / 1000, ratio, conc);
    lowTotalUs = 0; pulses = 0; windowStart = millis();
  }
}

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

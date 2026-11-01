// WiFi connection with backoff, and NTP time without blocking the loop.
// netInit() once, netPoll() every loop. nowUtc() is 0 until the first sync.
#pragma once
#include <rpcWiFi.h>
#include <WiFiUdp.h>
#include "secrets.h"
#include "ha_proto.h"  // Backoff
#include "wifi_pick.h"

#define NTP_RESYNC_MS 3600000UL
#define NTP_RETRY_MS  10000UL
#define NTP_TIMEOUT_MS 3000UL

static Backoff netBackoff;
static uint32_t netNextTry = 0;
static bool netWasUp = false;
static WiFiUDP ntpUdp;
static uint32_t ntpEpoch = 0, ntpAtMs = 0;      // last sync
static uint32_t ntpSentMs = 0, ntpNextMs = 0;   // request in flight / next request
static bool ntpWaiting = false;

inline bool netUp() { return WiFi.status() == WL_CONNECTED; }

inline uint32_t nowUtc() {
  return ntpEpoch ? ntpEpoch + (millis() - ntpAtMs) / 1000 : 0;
}

// Scans, then joins the strongest AP for WIFI_SSID with security the chip
// supports (see wifi_pick.h). Blocks for the scan (~3 s) and the join.
inline void netConnect() {
  Serial.println("wifi: scanning");
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("wifi: scan found nothing"); return; }
  if (n > 40) n = 40;
  BssInfo list[40];
  for (int i = 0; i < n; i++)
    list[i] = {WiFi.RSSI(i), WiFi.channel(i), (uint8_t)WiFi.encryptionType(i), WiFi.SSID(i) == WIFI_SSID};
  int pick = pickBss(list, n);
  if (pick < 0) { Serial.println("wifi: no usable AP for SSID"); WiFi.scanDelete(); return; }
  uint8_t bssid[6];
  memcpy(bssid, WiFi.BSSID(pick), 6);
  Serial.print("wifi: joining ch"); Serial.print(list[pick].channel);
  Serial.print(" rssi "); Serial.println(list[pick].rssi);
  WiFi.scanDelete();
  WiFi.begin(WIFI_SSID, WIFI_PASS, list[pick].channel, bssid);
}

inline void netInit() {
  WiFi.mode(WIFI_STA);
  netConnect();
  netNextTry = millis() + netBackoff.next();
}

inline void ntpPoll() {
  uint32_t ms = millis();
  if (ntpWaiting) {
    if (ntpUdp.parsePacket() >= 48) {
      uint8_t pkt[48];
      ntpUdp.read(pkt, 48);
      uint32_t secs = (uint32_t)pkt[40] << 24 | (uint32_t)pkt[41] << 16 | (uint32_t)pkt[42] << 8 | pkt[43];
      if (secs > 2208988800UL) {
        ntpEpoch = secs - 2208988800UL;
        ntpAtMs = ms - (ms - ntpSentMs) / 2;  // half the round trip
        ntpNextMs = ms + NTP_RESYNC_MS;
        Serial.print("ntp: "); Serial.println(ntpEpoch);
      } else {
        ntpNextMs = ms + NTP_RETRY_MS;
      }
      ntpWaiting = false;
      ntpUdp.stop();
    } else if (ms - ntpSentMs > NTP_TIMEOUT_MS) {
      ntpWaiting = false;
      ntpUdp.stop();
      ntpNextMs = ms + NTP_RETRY_MS;
      Serial.println("ntp: timeout");
    }
    return;
  }
  if ((int32_t)(ms - ntpNextMs) < 0) return;
  uint8_t pkt[48] = {0};
  pkt[0] = 0x1B;  // LI 0, version 3, client mode
  ntpUdp.begin(2390);
  ntpUdp.beginPacket("pool.ntp.org", 123);
  ntpUdp.write(pkt, 48);
  ntpUdp.endPacket();
  ntpSentMs = ms;
  ntpWaiting = true;
}

inline void netPoll() {
  bool up = netUp();
  if (up && !netWasUp) {
    Serial.print("wifi: up "); Serial.println(WiFi.localIP());
    netBackoff.reset();
    ntpNextMs = millis();
  }
  if (!up && netWasUp) Serial.println("wifi: down");
  netWasUp = up;
  if (up) { ntpPoll(); return; }
  if ((int32_t)(millis() - netNextTry) >= 0) {
    netConnect();
    netNextTry = millis() + netBackoff.next();
  }
}

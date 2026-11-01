// Picks which access point to join. Pure, host-tested (test/run.sh).
//
// Why: the home SSID is broadcast on 5 GHz with a security mode the RTL8720
// firmware (2.1.3) reports as unknown (WIFI_AUTH_MAX = 6, likely WPA3) and on
// 2.4 GHz with WPA2. A plain WiFi.begin() picks the stronger 5 GHz AP and then
// hangs forever inside the RPC call. So we connect by BSSID to the strongest
// AP whose security the chip understands.
#pragma once
#include <stdint.h>

struct BssInfo { int32_t rssi; int32_t channel; uint8_t enc; bool ssidMatch; };

// enc values follow wifi_auth_mode_t: 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA/WPA2.
inline bool bssSupported(uint8_t enc) { return enc >= 2 && enc <= 4; }

// Index of the strongest matching AP with supported security, or -1.
inline int pickBss(const BssInfo* list, int n) {
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (!list[i].ssidMatch || !bssSupported(list[i].enc)) continue;
    if (best < 0 || list[i].rssi > list[best].rssi) best = i;
  }
  return best;
}

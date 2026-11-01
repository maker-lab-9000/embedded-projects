// Hardware watchdog (SAMD51 WDT, 1.024 kHz ULP clock).
// Why: an rpcWiFi TCP connect to a host that is down blocks inside the RTL8720
// RPC call for many minutes (measured >5.5 min), freezing screen and buttons.
//
// If wdtFeed() isn't called for 8 s, the early-warning interrupt holds the
// RTL8720 WiFi chip in reset; 8 s later the WDT resets the board. The chip
// needs that long reset: after a plain watchdog reset (only the library's
// 100 ms reset at boot) its scans came back empty and then hung, and
// resetting it again from setup() desynced the eRPC link (measured 2026-11-02).
#pragma once
#include <Arduino.h>

// Breadcrumbs in backup RAM (survives resets): the last step reached before a
// watchdog reset, and whether the early-warning handler ran.
#define WDT_CRUMBS ((volatile uint32_t*)0x47000000UL)  // BKUPRAM
#define WDT_MAGIC 0x57445443UL
inline void wdtCrumb(uint32_t step) { WDT_CRUMBS[1] = step; }

inline bool wdtCausedLastReset() { return RSTC->RCAUSE.reg & RSTC_RCAUSE_WDT; }

extern "C" void WDT_Handler() {
  WDT->INTFLAG.reg = WDT_INTFLAG_EW;
  WDT_CRUMBS[2]++;
  digitalWrite(RTL8720D_CHIP_PU, LOW);  // keep the WiFi chip in reset until the board resets
  while (true) {}
}

// Prints and clears the breadcrumbs from before the last reset.
inline void wdtReport() {
  if (WDT_CRUMBS[0] != WDT_MAGIC) { WDT_CRUMBS[0] = WDT_MAGIC; WDT_CRUMBS[1] = 0; WDT_CRUMBS[2] = 0; return; }
  Serial.print("last step "); Serial.print(WDT_CRUMBS[1]);
  Serial.print(", early warnings so far "); Serial.println(WDT_CRUMBS[2]);
}

inline void wdtInit() {
  WDT->CTRLA.reg = 0;
  while (WDT->SYNCBUSY.reg) {}
  WDT->CONFIG.reg = WDT_CONFIG_PER_CYC16384;        // reset after 16 s
  WDT->EWCTRL.reg = WDT_EWCTRL_EWOFFSET_CYC8192;     // early warning after 8 s
  WDT->INTFLAG.reg = WDT_INTFLAG_EW;
  WDT->INTENSET.reg = WDT_INTENSET_EW;
  NVIC_SetPriority(WDT_IRQn, 0);
  NVIC_EnableIRQ(WDT_IRQn);
  WDT->CTRLA.reg = WDT_CTRLA_ENABLE;
  while (WDT->SYNCBUSY.reg) {}
}

inline void wdtFeed() {
  if (!(WDT->SYNCBUSY.reg & WDT_SYNCBUSY_CLEAR)) WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
}

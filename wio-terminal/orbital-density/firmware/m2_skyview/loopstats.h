// loopstats.h — loop-timing counters: the regression metric for every sensor added to loop().
// A blocking call shows up as loopMaxMsLast well above ~22 ms AND a rising
// gps.failedChecksum() rate, because the 256-byte UART RX buffer overflows at 115200 baud.
#pragma once
#include <Arduino.h>

uint32_t loopMaxMsLast = 0;    // longest iteration in the previous 1 s window, ms (rounded up)
uint32_t loopIterLast  = 0;    // iterations in the previous 1 s window
static uint32_t loopMaxUs = 0, loopIter = 0, loopPrevUs = 0;

// Call first thing in every loop() iteration.
inline void loopStatsTick() {
  uint32_t now = micros();
  if (loopPrevUs) { uint32_t dt = now - loopPrevUs; if (dt > loopMaxUs) loopMaxUs = dt; }
  loopPrevUs = now;
  loopIter++;
}

// Call once per second (from the existing 1 Hz block) to close the window.
inline void loopStatsRoll() {
  loopMaxMsLast = (loopMaxUs + 999) / 1000;
  loopIterLast  = loopIter;
  loopMaxUs = 0; loopIter = 0;
}

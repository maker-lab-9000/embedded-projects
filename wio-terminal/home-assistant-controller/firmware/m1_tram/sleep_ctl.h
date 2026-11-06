// Display sleep: button C toggles the backlight; while dark, any key wakes
// the display and is consumed. Pure, host-tested (test/run.sh).
#pragma once
#include <stdint.h>

enum Key : uint8_t { KEY_A, KEY_B, KEY_C, KEY_JOY_UP, KEY_JOY_DOWN, KEY_JOY_LEFT, KEY_JOY_RIGHT, KEY_JOY_PRESS };
enum SleepResult : uint8_t { SLEEP_PASS, SLEEP_TURN_OFF, SLEEP_TURN_ON };

struct SleepCtl { bool dark = false; };

inline SleepResult sleepOnKey(SleepCtl& z, Key k) {
  if (k == KEY_C) { z.dark = !z.dark; return z.dark ? SLEEP_TURN_OFF : SLEEP_TURN_ON; }
  if (z.dark) { z.dark = false; return SLEEP_TURN_ON; }
  return SLEEP_PASS;
}

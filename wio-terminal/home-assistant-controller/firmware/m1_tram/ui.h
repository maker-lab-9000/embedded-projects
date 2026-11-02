// Buttons, display sleep and the page table. Joystick left/right switches pages.
#pragma once
#include <TFT_eSPI.h>
#include "sleep_ctl.h"
#include "backlight.h"

struct Page {
  const char* name;
  void (*draw)(bool full);
  bool (*key)(Key k);  // keys the page handles itself (true = used, before page switching); may be null
};

static const uint8_t KEY_PINS[] = {WIO_KEY_A, WIO_KEY_B, WIO_KEY_C, WIO_5S_UP, WIO_5S_DOWN,
                                   WIO_5S_LEFT, WIO_5S_RIGHT, WIO_5S_PRESS};  // order = enum Key
#define KEY_COUNT 8
#define DEBOUNCE_MS 30

static bool keyDown[KEY_COUNT];
static uint32_t keyChanged[KEY_COUNT];
static SleepCtl uiSleep;
static const Page* uiPages;
static uint8_t uiPageCount = 0, uiPage = 0;
static bool uiNeedFull = true;

inline void uiInit(const Page* pages, uint8_t count) {
  uiPages = pages;
  uiPageCount = count;
  for (uint8_t i = 0; i < KEY_COUNT; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);
  backlightInit(BACKLIGHT_DEFAULT);
}

inline bool uiDark() { return uiSleep.dark; }

static void uiKey(Key k) {
  switch (sleepOnKey(uiSleep, k)) {
    case SLEEP_TURN_OFF: backlightSet(0); return;
    case SLEEP_TURN_ON: backlightSet(BACKLIGHT_DEFAULT); uiNeedFull = true; return;
    case SLEEP_PASS: break;
  }
  if (uiPages[uiPage].key && uiPages[uiPage].key(k)) return;
  if (uiPageCount > 1 && (k == KEY_JOY_LEFT || k == KEY_JOY_RIGHT)) {
    uiPage = (uiPage + (k == KEY_JOY_RIGHT ? 1 : uiPageCount - 1)) % uiPageCount;
    uiNeedFull = true;
  }
}

// Serial commands in the sketch: next page (even while a page holds the
// joystick), and simulated key presses.
inline void uiNextPage() { uiPage = (uiPage + 1) % uiPageCount; uiNeedFull = true; }
inline void uiInjectKey(Key k) { uiKey(k); }

// Debounced press edges -> uiKey(); redraws the current page unless dark.
inline void uiPoll() {
  uint32_t ms = millis();
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    bool down = digitalRead(KEY_PINS[i]) == LOW;
    if (down != keyDown[i] && ms - keyChanged[i] > DEBOUNCE_MS) {
      keyDown[i] = down;
      keyChanged[i] = ms;
      if (down) uiKey((Key)i);
    }
  }
  if (uiSleep.dark) return;  // no drawing while the backlight is off
  bool full = uiNeedFull;
  uiNeedFull = false;
  uiPages[uiPage].draw(full);
}

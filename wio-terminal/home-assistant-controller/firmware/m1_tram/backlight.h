// LCD backlight brightness 0..100 %.
// The backlight pin (PC05) has no PWM, so, as in Seeed's Wio Terminal
// backlight example, TC0 makes an 8-bit PWM signal that reaches the pin via
// CCL LUT0 -> EVSYS channel 0 -> PORT event output. After backlightInit()
// the pin is driven by hardware; use backlightSet(), not digitalWrite().
#pragma once
#include <Arduino.h>

#define BACKLIGHT_DEFAULT 18  // percent

inline void backlightSet(uint8_t percent) {
  if (percent > 100) percent = 100;
  TC0->COUNT8.CC[0].reg = percent;
  while (TC0->COUNT8.SYNCBUSY.bit.CC0) {}
}

inline void backlightInit(uint8_t percent) {
  // Peripheral clocks from GCLK0: TC0/TC1, EVSYS channel 0, CCL.
  GCLK->PCHCTRL[TC0_GCLK_ID].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
  while (!GCLK->PCHCTRL[TC0_GCLK_ID].bit.CHEN) {}
  GCLK->PCHCTRL[EVSYS_GCLK_ID_0].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
  while (!GCLK->PCHCTRL[EVSYS_GCLK_ID_0].bit.CHEN) {}
  GCLK->PCHCTRL[CCL_GCLK_ID].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
  while (!GCLK->PCHCTRL[CCL_GCLK_ID].bit.CHEN) {}
  MCLK->APBAMASK.bit.TC0_ = 1;
  MCLK->APBBMASK.bit.EVSYS_ = 1;
  MCLK->APBCMASK.bit.CCL_ = 1;

  // PORT: PC05 is set from event 0 (PORTEI = 1, EVACT0 = OUT, PID0 = 5).
  PORT->Group[2].DIRSET.reg = 1u << 5;
  PORT->Group[2].EVCTRL.reg = 0x85;
  // EVSYS: channel 0 = CCL LUT0 output, asynchronous; user PORT_EV0 listens to it.
  EVSYS->USER[EVSYS_ID_USER_PORT_EV_0].reg = 0x01;  // channel 0 (+1)
  EVSYS->Channel[0].CHANNEL.reg = EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_CCL_LUTOUT_0) | EVSYS_CHANNEL_PATH_ASYNCHRONOUS;
  // CCL LUT0: output = input 0 = TC0 WO0 (truth table 0xAA), event output enabled.
  CCL->CTRL.reg = CCL_CTRL_SWRST;
  CCL->SEQCTRL[0].reg = 0;
  CCL->LUTCTRL[0].reg = CCL_LUTCTRL_TRUTH(0xAA) | CCL_LUTCTRL_LUTEO | CCL_LUTCTRL_INSEL0(0x6) | CCL_LUTCTRL_ENABLE;
  CCL->CTRL.reg = CCL_CTRL_ENABLE;

  // TC0: 8-bit normal PWM, 120 MHz / 16 / 100 = 75 kHz, duty = CC0 / 100.
  TC0->COUNT8.CTRLA.reg = TC_CTRLA_SWRST;
  while (TC0->COUNT8.SYNCBUSY.bit.SWRST) {}
  TC0->COUNT8.CTRLA.reg = TC_CTRLA_MODE_COUNT8 | TC_CTRLA_PRESCALER_DIV16 | TC_CTRLA_PRESCSYNC_PRESC;
  TC0->COUNT8.WAVE.reg = TC_WAVE_WAVEGEN_NPWM;
  TC0->COUNT8.PER.reg = 100;
  TC0->COUNT8.CC[0].reg = percent > 100 ? 100 : percent;
  TC0->COUNT8.CC[1].reg = 0;
  TC0->COUNT8.DBGCTRL.bit.DBGRUN = 1;
  TC0->COUNT8.INTFLAG.reg = 0x33;
  while (TC0->COUNT8.SYNCBUSY.reg) {}
  TC0->COUNT8.CTRLA.bit.ENABLE = 1;
  while (TC0->COUNT8.SYNCBUSY.bit.ENABLE) {}
}

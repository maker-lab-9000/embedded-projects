# Sensor calibration log

Measured with the ESP32-S3's 12-bit ADC (0–4095, 11 dB attenuation,
`analogReadMilliVolts` for the mV column), sensor powered from the M5StickS3's
`3V3_L2` HAT pin, median-of-15 sampling. Readings were stable to ±5 counts.

| Sensor | Pin | Date       | raw dry air (0 %) | raw in water (100 %) | Notes |
|--------|-----|------------|-------------------|----------------------|-------|
| #2     | G7  | 2026-08-17 | 3573 (≈2875 mV)   | 1392 (≈1172 mV)      | delta 2181 counts — works fine at 3.3 V; dry-air level well below the ~3100 mV ADC ceiling, no clipping |

The `3V3_L2` rail is live with M5Unified's default `M5.begin()` — no PMIC
rail-enable call was needed.

**Serial quirk:** opening the USB serial port resets the device (native USB;
DTR/RTS map to the S3's reset/boot straps). Harmless for calibration, but it
restarts the trend warm-up — and a wrong DTR/RTS combination on open can drop
the chip into download mode (dark screen, USB product "ESP32_S3"; single-click
the side button to boot normally). Hold DTR and RTS inactive when opening.

Recalibrate roughly monthly (probe coating ages) and whenever a sensor is
swapped — boards vary unit to unit, so each sensor needs its own row. Sensor
depth notes from the Wio project apply here too: the water anchor is measured
at the insertion line, and any change of probe depth in soil shifts readings,
so restart trends (KEY2 hold) after moving the probe.

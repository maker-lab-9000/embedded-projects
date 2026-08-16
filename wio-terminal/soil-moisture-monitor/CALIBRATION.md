# Sensor calibration log

Measured with 12-bit ADC (0–4095), sensor powered from the Wio Terminal's
3.3 V pin, median-of-15 sampling. Readings were stable to ±2 counts.

| Sensor | Pin | Date       | raw dry air (0 %) | raw in water (100 %) | Notes |
|--------|-----|------------|-------------------|----------------------|-------|
| #1     | A0  | 2026-08-16 | 3687              | 1568                 | ~35 s settling time after immersion; delta 2119 counts — works fine at 3.3 V |

**Insertion depth matters.** On 2026-08-16 the probe was pushed from a
shallow position down to the insertion line and the soil reading rose from
40 % to ~56 % with no watering. The air/water anchors stay valid (the water
anchor was measured at that same line), but any depth change shifts soil
readings — so the probe must stay put, and the trend history was restarted
with a fresh SD dataset when it was seated at its final depth.

Recalibrate roughly monthly (probe coating ages) and whenever a sensor is
swapped — boards vary unit to unit, so each sensor needs its own row.

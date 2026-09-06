# Wio Terminal Multi-Layer Sky Observatory

## Revised project concept

The revised device is best treated as a small **multi-layer sky observatory**, with each sensor answering a different question rather than simply collecting unrelated readings.

The dust sensor can be excluded temporarily. The proposed instrument combines:

| Sensor | Observation layer | Primary role |
|---|---|---|
| Air530 | Orbit / positioning | GNSS observations, location, altitude, and precise UTC |
| Adafruit TSL2591 | Optical sky | Visible and near-infrared sky brightness |
| MLX90614ESF-BCC | Atmosphere / thermal sky | Long-wave infrared sky brightness temperature and cloud response |
| Adafruit MMC5603 | Magnetic field / orientation | Magnetic vector, field magnitude, and heading |
| SHT40 | Atmosphere | Air temperature and relative humidity |
| Pressure sensor | Atmosphere | Atmospheric pressure |

Together, these form a portable, GPS-synchronized optical, thermal, atmospheric, and geomagnetic sky-observation station.

> **Implementation note (2026-09-03):** the SHT40 and the separate pressure sensor are superseded by the BME280 already in the firmware. The MLX90614 (GY-906) is deferred until its header is soldered; the firmware carries it behind `MLX_ENABLED`. The dust sensor is compiled out for now. See `docs/superpowers/specs/2026-09-02-sky-observatory-sensors-design.md` and README "Sky-observatory sensors".

```text
                         SKY / SPACE
                             │
             ┌───────────────┼───────────────┐
             │               │               │
             ▼               ▼               ▼
        TSL2591         MLX90614          Air530 GNSS
      optical sky      thermal sky       position/time
             │               │               │
             └───────┐       │       ┌───────┘
                     ▼       ▼       ▼
                    WIO TERMINAL
                         │
                         │
                MMC5603 magnetometer
                         │
                 orientation / field
                         │
                         ▼
                 timestamped record
                         │
             Wi-Fi / SD card / USB serial
                         │
                         ▼
               PC / database / Grafana
```

## I²C integration

The three new sensors can share the Wio Terminal's I²C bus without an address collision:

| Device | Default I²C address |
|---|---:|
| TSL2591 | `0x29` |
| MMC5603 | `0x30` |
| MLX90614 | `0x5A` |

The TSL2591 also uses `0x28` internally, so that address should be considered unavailable.

```text
Wio Terminal
 Grove I²C
     │
     │ SDA / SCL / 3.3 V / GND
     ▼
┌───────────────┐
│ I²C hub       │
└─┬────┬────┬───┘
  │    │    └── MLX90614  0x5A
  │    └─────── MMC5603   0x30
  └──────────── TSL2591   0x29
```

A Grove-to-STEMMA QT/Qwiic adapter or hub would make the two Adafruit boards easier to connect and avoid loose jumper wiring. Standardize the sensor side at **3.3 V**. Before powering a generic GY-906 breakout, verify its voltage regulator and I²C pull-up arrangement.

> **Implemented:** Grove I²C Hub on the LEFT Grove port carrying BME280, TSL2591 (Adafruit 4528 Grove-to-QT cable) and MMC5603 (second 4528 cable); the Air530Z moved to the battery-chassis `RX TX` socket (= `Serial1`). The bus stays at 100 kHz. Wiring diagram and chassis socket map are in the README.

## 1. TSL2591 optical-sky channel

The TSL2591 should measure optical sky brightness. Do not store only calculated lux; retain the raw readings and the acquisition settings as well:

```text
tsl_full
tsl_ir
tsl_visible
tsl_lux
tsl_gain
tsl_integration_ms
```

Useful derived values include:

```text
IR fraction      = IR / Full
Visible fraction = Visible / Full
```

Recording gain and integration time is important because automatic changes to either setting can make long-term comparisons misleading.

### Physical mounting

Point the sensor toward the zenith through a short, matte-black baffled tube:

```text
             SKY
              ↑
          ╔══════╗
          ║      ║  matte-black baffle
          ║      ║
          ║      ║
          ╚══╤═══╝
             │
          TSL2591
```

The baffle gives the sensor a more repeatable field of view and reduces contamination from streetlights, windows, passing cars, horizon glow, and the Wio Terminal display.

## 2. MLX90614 thermal-sky channel

Mount the MLX90614 beside the TSL2591 and point it toward the zenith, but give it a separate aperture.

```text
       OPTICAL              THERMAL IR

         sky                    sky
          ↑                      ↑
       │     │                \     /
       │ TSL │                 \   /
       │     │                  \ /
       └─────┘                  MLX
```

Do not place both sensors behind a shared ordinary glass or acrylic window. Such materials can work for visible light but strongly interfere with the long-wave infrared band used by the MLX90614. For an early prototype, leave the MLX aperture exposed and recessed under a protective hood.

Store both measurements supplied by the sensor:

```text
mlx_ambient_c
mlx_object_c
```

Calculate:

```text
mlx_sky_delta = mlx_ambient_c - mlx_object_c
```

The reported object value should be described as **IR sky brightness temperature**, not the literal physical temperature of space. Clear skies generally appear much colder in long-wave infrared than clouds, making the measurement useful for cloud detection and atmospheric-radiation experiments.

## 3. MMC5603 magnetic and orientation channel

The magnetometer should not be mounted directly beside the Wio Terminal or its other electronics. Put it on a **15–30 cm non-magnetic arm** and determine the necessary separation experimentally.

```text
                 TSL       MLX
                  ↑         ↑

              ┌───────────────┐
              │ Wio Terminal  │
              │ GPS / sensors │
              └───────┬───────┘
                      │
                      │ 15–30 cm non-magnetic arm
                      │
                   MMC5603
                     XYZ
```

Nearby current-carrying wires, magnets, batteries, speakers, steel screws, the display, Wi-Fi activity, and structural metal can all distort the result. Test the sensor at increasing distances while switching these components on and off.

Initially record:

```text
Bx
By
Bz
B_total = sqrt(Bx² + By² + Bz²)
magnetic_heading
```

The first practical use is instrument orientation. The second is recording local magnetic-field changes over time. Interpret small apparent geomagnetic variations cautiously: vehicles, elevators, wiring, nearby steel, and movement of the instrument will normally dominate subtle space-weather effects.

### Future pointing and tracking use

If the project later gains a pan/tilt sensor head, the magnetometer can contribute to coarse orientation:

```text
Target: ISS

Predicted:
Azimuth:              247.4°
Elevation:             61.8°

Measured orientation:
Azimuth:              246.9°
Tilt:                  62.3°

Calculated error:       0.7°
```

For accurate pointing, the magnetometer should ultimately be complemented by a tilt sensor or IMU and calibrated for hard-iron and soft-iron distortion.

## 4. Air530 as the synchronization layer

Treat GNSS as the clock and positioning foundation for every observation, rather than merely another sensor. Associate every stored observation with:

```text
UTC timestamp
latitude
longitude
altitude
GNSS fix quality
satellite count
```

This creates a common timebase for comparing optical, thermal, magnetic, and atmospheric measurements and for correlating them later with Moon positions, satellite passes, weather data, or other ephemerides.

## Unified observation model

Structure the firmware around an `Observation`, not around individual sensor drivers:

```text
Sensor drivers
     ↓
raw measurements
     ↓
calibration
     ↓
derived measurements
     ↓
Observation
     ↓
 ┌───┼────┐
 │   │    │
LCD  SD  Wi-Fi
```

An illustrative C++ model:

```cpp
struct Observation {
    Timestamp time;
    Position position;

    OpticalMeasurement optical;
    ThermalSkyMeasurement thermal;
    MagneticMeasurement magnetic;
    AtmosphereMeasurement atmosphere;
    GNSSMeasurement gnss;
};
```

Example serialized observation:

```json
{
  "time": "2026-09-02T21:34:15.200Z",
  "position": {
    "lat": 52.52,
    "lon": 13.40,
    "altitude_m": 45.0,
    "fix": "3D"
  },
  "optical": {
    "full_raw": 605,
    "visible_raw": 428,
    "ir_raw": 177,
    "lux": 0.031,
    "gain": "high",
    "integration_ms": 600
  },
  "thermal": {
    "ambient_c": 18.2,
    "sky_brightness_c": -27.4,
    "delta_c": 45.6
  },
  "magnetic": {
    "x_uT": 21.4,
    "y_uT": -4.7,
    "z_uT": 42.1,
    "total_uT": 47.5,
    "heading_deg": 247.0
  },
  "atmosphere": {
    "temperature_c": 17.8,
    "humidity_pct": 61.2,
    "pressure_hpa": 1014.2
  },
  "gnss": {
    "satellite_count": 23,
    "hdop": 0.9
  }
}
```

## Sampling design

Use independent non-blocking schedules rather than placing every sensor behind a single `delay(1000)` loop.

| Component | Suggested initial rate | Reason |
|---|---:|---|
| Air530 GNSS | 1 Hz | Position and time synchronization |
| MMC5603 | 10 Hz | Orientation and magnetic changes |
| TSL2591 | 1–2 Hz | Useful rate is limited by integration time |
| MLX90614 | 1–2 Hz | Sky conditions normally change slowly |
| SHT40 | 0.2–1 Hz | Environmental conditions change slowly |
| Pressure sensor | 0.2–1 Hz | Pressure changes slowly |
| Display | 2–5 Hz | Responsive user interface |
| Persistent observation log | 1 Hz | Manageable, correlated dataset |

Illustrative loop:

```cpp
void loop() {
    if (timeForGNSS())
        readGNSS();

    if (timeForMagnetometer())
        readMMC5603();

    if (timeForOptical())
        readTSL2591();

    if (timeForThermal())
        readMLX90614();

    if (timeForAtmosphere())
        readEnvironment();

    if (timeForDisplay())
        updateDisplay();

    if (timeForLog())
        createObservationRecord();
}
```

The Wio Terminal has ample compute capacity for this acquisition workload.

## Suggested Wio Terminal screens

### Sky

```text
      SKY OBSERVATION

Visible      0.034 lux
Near IR      31.2 %
Sky IR       -28.4 °C
Δ Sky         46.1 °C

Condition    CLEAR
```

### GNSS / space

```text
        GNSS / SKY

GPS       8
Galileo   6
BeiDou    4
GLONASS   5

Fix       3D
UTC       21:43:18

Heading   247°
```

> Constellation-specific satellite counts depend on what the Air530 firmware exposes in its NMEA output and what the GNSS library parses.
>
> Galileo is not receivable on the Air530Z's AT6558R; the real page shows GPS / GLONASS / BeiDou / QZSS.

### Environment

```text
       ATMOSPHERE

Temp       17.8 °C
Humidity   61.2 %
Pressure 1014.2 hPa

B-field    47.5 µT
```

### Observation context

```text
       OBSERVATION

Moon
Az     134°
El      42°

Sky optical   0.034 lx
Sky thermal  -28.4 °C
Cloud index   CLEAR
```

## Mechanical layout

Do not build a single sealed sensor box. Use a central body with deliberately separated sensing areas:

```text
                    SKY

              ┌─────┐ ┌─────┐
              │ TSL │ │ MLX │
              │tube │ │hood │
              └──┬──┘ └──┬──┘
                 │       │
        ┌────────┴───────┴────────┐
        │       WIO TERMINAL      │
        │                         │
        │ GNSS / Temp / Pressure  │
        └───────────┬─────────────┘
                    │
                    │ non-magnetic arm
                    │
                 MMC5603
```

The major physical rules are:

1. Give the TSL2591 a matte-black optical baffle.
2. Give the MLX90614 a separate, unobstructed long-wave IR aperture.
3. Keep the magnetometer away from electronics, magnets, current-carrying wires, and ferrous fasteners.
4. Keep the environmental temperature and humidity sensor ventilated and away from heat generated by the Wio Terminal.
5. Mount the GNSS antenna with a clear view of the sky.

This physical separation is likely to improve measurement quality more than firmware optimization.

## Recommended implementation order

1. Define the unified observation schema and units.
2. Scan the I²C bus and verify every address.
3. Integrate each sensor independently and log raw readings.
4. Replace blocking delays with independent sampling schedules.
5. Add calibration metadata and sensor status flags.
6. Assemble the separated mechanical layout.
7. Test magnetic interference at different arm lengths.
8. Test optical and thermal response under clear, cloudy, moonlit, and urban-light conditions.
9. Add SD-card, USB serial, MQTT, or Wi-Fi export.
10. Add derived conditions only after collecting enough local baseline data.

## Potential derived measurements

Once the raw dataset is reliable, the following derived channels become useful:

| Derived measurement | Inputs | Purpose |
|---|---|---|
| Optical IR fraction | TSL full + IR channels | Compare visible and near-IR response |
| Thermal sky delta | MLX ambient + object temperature | Cloud and atmospheric-radiation indicator |
| Magnetic magnitude | MMC X/Y/Z | Track total local magnetic field |
| Magnetic heading | MMC X/Y, plus calibration | Approximate instrument azimuth |
| Cloud index | Thermal delta + humidity + local baseline | Experimental sky-condition classification |
| Moonlight response | TSL + GNSS time/location + lunar ephemeris | Relate optical brightness to Moon altitude and phase |
| Atmospheric optical state | TSL + MLX + humidity + pressure | Distinguish optical brightness from thermal cloud response |

Avoid fixed universal thresholds at the beginning. Urban light, humidity, seasonal conditions, enclosure geometry, and sensor-to-sensor variation make a locally measured baseline much more useful.

## Reference documentation

- [Wio Terminal I²C overview](https://wiki.seeedstudio.com/Wio-Terminal-IO-I2C/)
- [Adafruit TSL2591 wiring and test](https://learn.adafruit.com/adafruit-tsl2591/wiring-and-test)
- [Adafruit TSL2591 pinouts](https://learn.adafruit.com/adafruit-tsl2591/pinouts)
- [Adafruit MMC5603 pinouts](https://learn.adafruit.com/adafruit-mmc5603-triple-axis-magnetometer/pinouts)
- [MLX90614 datasheet](https://www.melexis.com/-/media/files/documents/datasheets/mlx90614-datasheet-melexis.pdf)


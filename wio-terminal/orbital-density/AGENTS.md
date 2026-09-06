# Repository Guidelines

## Project Structure & Module Organization

This repository contains Arduino firmware for the Orbital Density Wio Terminal project. Firmware lives under `firmware/`, with each sketch in its own Arduino-compatible directory:

- `firmware/m1_gps/`: Serial bring-up for the Air530 GNSS module.
- `firmware/m1_dust/`: Serial bring-up for the Grove dust sensor.
- `firmware/m1_bme280/`: Serial bring-up and I2C scan for the BME280.
- `firmware/m1_i2c_scan/`: Hub bus scan plus Serial1 NMEA byte count with auto-baud (confirms the chassis UART socket).
- `firmware/m1_pinsweep/`: Counts edges on every 40-pin-header GPIO; finds which header pin a chassis-socket signal lands on.
- `firmware/m1_tsl2591/`, `firmware/m1_mmc5603/`, `firmware/m1_mlx90614/` (deferred): Serial bring-up per sky sensor.
- `firmware/m2_skyview/`: Current full application: the sketch plus header-only modules in the same folder (`loopstats.h`, `i2c_bus.h`, `tsl2591.h`, `ring.h`, `mmc5603.h`, `mlx90614.h` (deferred), `observation.h`, `pages_sensors.h`). UI pages, SD logging (`/gps.csv` 60 s, `/obs.csv` 1 Hz), GNSS parsing, battery, environmental and sky sensing.

Documentation is in `README.md` and `PLAN.md` (original milestone plan, historical). The sky-observatory sensor work has its design spec in `docs/superpowers/specs/` and its task plan in `docs/superpowers/plans/`. Product screenshots and page images are stored in `docs/`.

## Build, Test, and Development Commands

Run commands from the repository root.

```sh
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal firmware/m2_skyview
```

Compiles the current full firmware. Use the same command with `firmware/m1_gps`, `firmware/m1_dust`, or `firmware/m1_bme280` for bring-up sketches.

```sh
arduino-cli board list
arduino-cli upload --fqbn Seeeduino:samd:seeed_wio_terminal -p /dev/cu.usbmodemXXX firmware/m2_skyview
```

Finds the Wio Terminal serial port, then uploads the sketch. Close serial monitors before uploading.

Required libraries include `TinyGPSPlus`, `Seeed Arduino FS`, `SparkFun BQ27441`, `Adafruit MMC56x3` (pulls in `Adafruit BusIO` and `Adafruit Unified Sensor`), and the Seeed SAMD core with bundled `TFT_eSPI`. `Adafruit MLX90614 Library` only once `MLX_ENABLED` is set to 1. The TSL2591 uses a local register-level driver because the Adafruit library blocks for 720 ms per read.

## Coding Style & Naming Conventions

Use Arduino/C++ style already present in the sketches: two-space indentation inside functions, compact helper functions, `camelCase` for functions and variables, and `UPPER_CASE` constants only when matching existing code. Keep hardware assumptions explicit in comments near the relevant code, especially wiring, voltage, and sensor timing constraints.

Keep existing GPS parsing, sky plot, chart and `/gps.csv` code in place; add new functionality as header modules in `firmware/m2_skyview/` (pattern: `xxxOk`, `xxxInit()`, non-blocking `xxxPoll()`), and limit edits to `m2_skyview.ino` to includes, `setup()`, `loop()`, the page table and the observation log. Nothing in `loop()` may block: the UART RX buffer is 256 bytes (~22 ms at 115200). `Serial.printf()` on this core truncates at ~80 characters; build long lines from several calls.

## Testing Guidelines

There is no automated test suite. Treat successful `arduino-cli compile` as the baseline gate for every firmware change. For behavior changes, validate on hardware when possible: serial output for `m1_*` sketches, page rendering and button navigation for `m2_skyview`, and `/gps.csv` / `/obs.csv` creation on microSD for logging changes. For any change to `loop()`, compare `loopMax` and `nmeaFail` in the 1 Hz status line against the baseline table in README before and after.

## Commit & Pull Request Guidelines

Recent commits use short, imperative subjects, often scoped with `orbital-density:` or `orbital-density docs:`. Follow that pattern, for example:

```text
orbital-density: add ambient light page
orbital-density docs: update wiring notes
```

Pull requests should describe the hardware affected, list compile commands run, note any hardware validation performed, and include updated screenshots for visible UI changes.

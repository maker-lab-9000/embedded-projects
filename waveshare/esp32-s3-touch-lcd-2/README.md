# Waveshare ESP32-S3-Touch-LCD-2 — Home Assistant touch dashboard

A 2-inch touch panel that shows Home Assistant data and controls devices, built with
**ESPHome** over the HA **native API** (no MQTT). Configuration lives in `esphome/lcd2.yaml`;
Home Assistant entity IDs are set in its `substitutions:` block.

## What it does

Six LVGL pages on the 2-inch touch screen, navigated with `<` / `>` buttons; the header shows
the clock, the page title and a green/red `HA` link indicator.

| Page | Content |
|---|---|
| **Rooms** (first) | Bedroom, Theo's room, Bathroom, Living Room: temperature large, humidity below |
| **Home** | Two thermostat tiles (current, set-point, mode; tap cycles heat → off → auto) and PM2.5 / PM10 coloured green/yellow/red |
| **Lights** | Philips, Tripod, Bedroom and Bedside lamps: on/off switch and a 0–255 brightness slider each |
| **Server** | CPU / GPU / NVMe temperatures, Proxmox CPU / RAM / disk as bar rows |
| **VMs** | Ubuntu VM CPU / RAM / disk / IO-wait, HA VM CPU / RAM / disk |
| **Storage** | BigData / JellyMedia usage, NVMe wear, disk temperatures, RAID md0 line, SMART health |

Values on Server / VMs / Storage colour green, yellow or red at the same thresholds as the HA
gauges. After 60 s without a touch the backlight turns off and LVGL pauses; any touch wakes it.
The backlight is also an HA `light`, and a `Battery Voltage` sensor is exposed.

<img src="docs/page-rooms.jpg" width="200" alt="Rooms page"> <img src="docs/page-home.jpg" width="200" alt="Home page"> <img src="docs/page-lights.jpg" width="200" alt="Lights page">
<img src="docs/page-server.jpg" width="200" alt="Server page"> <img src="docs/page-vms.jpg" width="200" alt="VMs page"> <img src="docs/page-storage.jpg" width="200" alt="Storage page">

Design: `docs/superpowers/specs/2026-09-03-ha-touch-dashboard-design.md`; task plan with results:
`docs/superpowers/plans/2026-09-03-ha-touch-dashboard.md`.

## Home Assistant setup

1. Settings → Devices & services: the device is discovered as `lcd2` (mDNS). Configure with the
   `api_encryption_key` from `esphome/secrets.yaml`.
2. On the device's ESPHome entry enable **"Allow the device to perform Home Assistant actions"**,
   otherwise switches, sliders and thermostat taps are refused silently.
3. Entities: `Backlight` (light), `Battery Voltage`, `WiFi Signal`.

## Build and flash

ESPHome runs from a Python 3.12 venv (the Mac's system Python is 3.9):

```sh
brew install python@3.12
/usr/local/opt/python@3.12/bin/python3.12 -m venv ~/.venvs/esphome
~/.venvs/esphome/bin/pip install -U esphome
cp esphome/secrets.yaml.example esphome/secrets.yaml      # then fill in; git-ignored
~/.venvs/esphome/bin/esphome config  esphome/lcd2.yaml    # validate
~/.venvs/esphome/bin/esphome run     esphome/lcd2.yaml    # compile + OTA (device lcd2.local)
~/.venvs/esphome/bin/esphome logs    esphome/lcd2.yaml
```

First flash over USB-C: `esphome run esphome/lcd2.yaml --device /dev/cu.usbmodemXXXX`. Put the
board in download mode once (hold BOOT, tap RESET, release BOOT); after that esptool resets the
S3 over its USB-serial/JTAG port by itself, and later builds go over the air. Logging is on the
native USB port (`USB_SERIAL_JTAG`); `esphome logs --device /dev/cu.usbmodemXXXX` reads it.

Quote every value in `secrets.yaml` (an unclosed `"` on the SSID line cost one flash cycle).

## Changing what is shown

Entity IDs and names live in the `substitutions:` block at the top of `esphome/lcd2.yaml`.
Adding a value tile or a bar row means copying one block in three places:

1. a substitution for the entity (and name),
2. a `homeassistant` import under `sensor:` whose `on_value` updates the label (and bar, and
   the green/yellow/red `if` chain for a threshold),
3. the widget under the page's `widgets:`.

Lamp cards are the same idea with a `binary_sensor` import for the on/off state and a `sensor`
import of the light's `brightness` attribute for the slider.

## Troubleshooting

- Red and blue swapped → `color_order: rgb`; negative image → `invert_colors: false`; rotated →
  `rotation`. The shipped values (`ST7789V`, `invert_colors: true`, `bgr`, `0`) were verified.
- No touches in the log → `skip_probe: true` under `touchscreen:`; mirrored axes → `transform:`.
  Shipped values verified against all four corners; no transform needed.
- A phantom touch at (63, 319) appears on release; keep interactive widgets off the very bottom edge.
- Tiles stay `--` and `HA` is red → the device is not adopted in HA or HA cannot reach it.
- Switch flips but nothing happens → the "Allow the device to perform Home Assistant actions"
  permission is off.
- `lvgl took a long time for an operation (~110 ms)` once at boot is the first full render; harmless.

## Board (from Waveshare's demo sources)

ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 240×320 IPS (ST7789T3 over SPI), CST816D touch
(I2C, addr 0x15), QMI8658 IMU, TF card slot, DVP camera connector, Li-ion charger (MX1.25),
USB-C (native USB).

| Function | GPIO |
|---|---|
| LCD SPI MOSI / SCLK / MISO | 38 / 39 / 40 |
| LCD CS / DC / RST / backlight PWM | 45 / 42 / none (tied to EN) / 1 |
| Touch CST816D + IMU QMI8658 I2C SDA / SCL | 48 / 47 (touch has no INT/RST wired to a GPIO) |
| TF card CS (shares the LCD SPI bus) | 41 |
| Battery voltage ADC (÷3 divider) | 5 |
| Camera DVP XCLK / PCLK / VSYNC / HREF | 8 / 9 / 6 / 4 |
| Camera SCCB SIOD / SIOC, PWDN | 21 / 16, 17 |
| Camera D0..D7 | 12, 13, 15, 11, 14, 10, 7, 2 |

Sources: Waveshare wiki and demo package (`ESP32-S3-Touch-LCD-2-Demo.zip`), schematic
`ESP32-S3-Touch-LCD-2-SchDoc.pdf`.

# ESP32-S3-Touch-LCD-2 Home Assistant Touch Dashboard — Design

**Date:** 2026-09-03
**Status:** approved direction (ESPHome + native API); design written, plan follows
**Board:** Waveshare ESP32-S3-Touch-LCD-2 (ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 240×320 ST7789T3, CST816D touch)

## 1. Goal and scope

A wall/desk touch panel that shows a handful of Home Assistant (HA) values and toggles a
handful of HA devices, with no PC in the loop.

**In scope (v1):** ESPHome firmware; Wi-Fi + HA native API (encrypted) + OTA; LVGL UI with
two pages (data tiles, controls) and a header (time, HA link state); backlight PWM with idle
dimming and touch wake; battery voltage sensor; everything parameterised by `substitutions:`
so entity IDs are edited in one place.

**Out of scope (v1), possible later:** camera (ESPHome `esp32_camera` streams to HA, not to
the local screen), TF card (external component only), QMI8658 IMU (external component;
could drive auto-rotation), Material Design icons font, more pages, MQTT.

**Decision 2026-09-03:** ESPHome with the HA native API, not MQTT and not hand-written
C++. Rationale: entity import (`homeassistant` sensors), service calls
(`homeassistant.action`), LVGL, OTA and logging come built in; the same dashboard in
Arduino + MQTT is several times the code and needs a broker plus discovery plumbing.

## 2. Hardware facts (from Waveshare's demo sources and schematic)

| Function | GPIO | Notes |
|---|---|---|
| LCD SPI MOSI / SCLK / MISO | 38 / 39 / 40 | ST7789T3, 240×320, IPS (colours inverted vs. plain ST7789) |
| LCD CS / DC / RST / BL | 45 / 42 / none / 1 | RST tied to EN; BL is PWM-capable |
| Touch + IMU I2C SDA / SCL | 48 / 47 | CST816D at 0x15; no INT or RST wired (poll) |
| TF card CS | 41 | shares the LCD SPI bus |
| Battery ADC | 5 | Waveshare demo: `V = adc × 3` (÷3 divider) |
| Camera DVP | XCLK 8, PCLK 9, VSYNC 6, HREF 4, SIOD 21, SIOC 16, PWDN 17, D0..D7 = 12,13,15,11,14,10,7,2 | unused in v1 |

Native USB on the S3 (no UART bridge): logging goes over `USB_SERIAL_JTAG`; first flash is
over USB, then OTA.

## 3. Architecture

```
Home Assistant  ── native API (TLS-like Noise encryption, mDNS discovery) ──  ESPHome on the S3
   entities  ──►  homeassistant sensor / text_sensor / binary_sensor  ──►  LVGL labels/switches
   actions   ◄──  homeassistant.action (homeassistant.turn_on / turn_off)  ◄──  LVGL switch on_value
   time      ──►  time: platform: homeassistant                          ──►  header clock
```

One YAML file, `esphome/lcd2.yaml`, built up task by task. Framework **esp-idf** (needed
for octal PSRAM at 80 MHz and the smoothest LVGL). Display via `mipi_spi` (model
`ST7789V`, explicit 240×320 dimensions, `invert_colors: true` for the IPS panel); touch via
`cst816` (polled, address 0x15); UI via the `lvgl` component with `buffer_size: 100%` in
PSRAM.

Secrets (`wifi_ssid`, `wifi_password`, `api_encryption_key`, `ota_password`,
`ap_password`) live in `esphome/secrets.yaml`, git-ignored; `secrets.yaml.example` is
committed.

## 4. UI

Portrait, 240 wide × 320 tall.

```
┌──────────────────────────┐ y=0
│ 12:34            HA ●    │ header (time, link state)
├──────────────────────────┤ y=32
│ ┌──────────┐ ┌──────────┐│
│ │ Living   │ │ Outside  ││  4 data tiles: name (small), value+unit (large)
│ │ 21.4 °C  │ │ 13.8 °C  ││
│ └──────────┘ └──────────┘│
│ ┌──────────┐ ┌──────────┐│
│ │ Power    │ │ Humidity ││
│ │  412 W   │ │  58 %    ││
│ └──────────┘ └──────────┘│
├──────────────────────────┤ y=284
│   [ Home ]   [Controls]  │ nav bar (two buttons)
└──────────────────────────┘ y=320
```

Page 2, "Controls": four rows, each an LVGL `switch` with a name label. The switch state
mirrors HA (via a `homeassistant` binary sensor → `lvgl.widget.update`), and flipping it
calls `homeassistant.turn_on` / `homeassistant.turn_off` on the entity, which works for
switches, lights, fans and input_booleans alike.

Idle: after 60 s without touch, the backlight turns off and LVGL pauses; any touch resumes
LVGL, redraws and turns the backlight back on. Backlight is also an HA `light` entity, so
brightness can be automated (e.g. dim at night).

Fonts: LVGL's built-in Montserrat set (`montserrat_14`, `_20`, `_28`); they include the
degree sign. No downloaded fonts in v1.

## 5. Parameterisation

```yaml
substitutions:
  tile1_entity: sensor.living_room_temperature
  tile1_name: "Living"
  tile1_format: "%.1f °C"
  ...
  sw1_entity: switch.desk_lamp
  sw1_name: "Desk lamp"
```

Adding a tile or a switch is copying one block in each of three places (substitution,
`homeassistant` import, LVGL widget); the plan shows the pattern once per kind.

## 6. HA-side requirements

- ESPHome integration (auto-discovers the device by mDNS; paste the API key once).
- On the device's ESPHome integration entry, enable **"Allow the device to perform Home
  Assistant actions"** — without it `homeassistant.action` calls are silently refused.
- Optional: the ESPHome add-on as an alternative build environment; this plan uses a local
  ESPHome in a Python 3.12 venv (the Mac's system Python is 3.9).

## 7. Verification

No unit tests; gates are: `esphome config` (validation) and `esphome compile` per task, then
on-device checks: colour/orientation test pattern (Task 3), touch coordinates in the log
(Task 4), tile values equal HA's (Task 5), a real device toggles from the panel and the panel
follows a toggle made in HA (Task 6), idle/wake and battery voltage (Task 7). Hardware steps
are the user's; the plan stops and asks at each.

## 8. Risks and fallbacks

- `mipi_spi` model presets: if `ST7789V` renders shifted or mirrored, use `rotation`,
  `offset_width/height`, or the legacy `st7789v` display platform (snippet in the plan).
- Colours: if red and blue swap, flip `color_order`; if the image is a negative, flip
  `invert_colors`.
- CST816D not detected at boot: set `skip_probe: true` (some CST816 variants only answer
  after the first touch).
- Touch axes mirrored/swapped: `transform: {mirror_x, mirror_y, swap_xy}` on the touchscreen.
- Battery ADC scale: verify against a multimeter; adjust the `multiply` filter.

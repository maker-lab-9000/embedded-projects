# Waveshare ESP32-S3-Touch-LCD-2 — Home Assistant touch dashboard

A 2-inch touch panel that shows Home Assistant data and controls devices, built with
**ESPHome** over the HA **native API** (no MQTT). Configuration lives in `esphome/lcd2.yaml`;
Home Assistant entity IDs are set in its `substitutions:` block.

Status: **planned** — see `docs/superpowers/specs/` (design) and `docs/superpowers/plans/`
(task plan with checkboxes). Build/flash commands are in the plan.

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

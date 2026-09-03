# ESP32-S3-Touch-LCD-2 HA Touch Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An ESPHome firmware for the Waveshare ESP32-S3-Touch-LCD-2 that shows four Home Assistant values and toggles four Home Assistant devices from an LVGL touch UI, over the HA native API.

**Architecture:** One ESPHome YAML (`esphome/lcd2.yaml`) grown task by task: base connectivity, display, touch + LVGL, data tiles, controls, idle/battery. HA entity IDs live in `substitutions:`. Values arrive via `homeassistant` sensor/binary_sensor imports; control goes out via `homeassistant.action`. esp-idf framework, octal PSRAM, `mipi_spi` display, `cst816` touch.

**Tech Stack:** ESPHome (current release, 2025.x; `mipi_spi` needs a 2025 release), Python 3.12 venv via Homebrew, LVGL (bundled in ESPHome), Home Assistant with the ESPHome integration.

**Spec:** `waveshare/esp32-s3-touch-lcd-2/docs/superpowers/specs/2026-09-03-ha-touch-dashboard-design.md`

## Global Constraints

- All commands run from the repo root `/Users/george.babanau/repos/embedded`. ESPHome lives in a venv: `~/.venvs/esphome/bin/esphome` (Task 1); the shorthand `esphome` below means that binary.
- YAML file: `waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`. Secrets: `esphome/secrets.yaml` (git-ignored; template `secrets.yaml.example`). Never commit `secrets.yaml`.
- Pins (verbatim from Waveshare's demos): LCD MOSI 38, SCLK 39, MISO 40, CS 45, DC 42, BL 1, no RST; touch/IMU I2C SDA 48, SCL 47, CST816D at `0x15`, no INT; TF CS 41; battery ADC 5 (×3).
- Framework `esp-idf`; `psram: mode: octal, speed: 80MHz`; logger over `USB_SERIAL_JTAG`.
- Gate per task: `esphome config <yaml>` then `esphome compile <yaml>` succeed, then the hardware checkpoint. Hardware steps (USB, download mode, pressing the screen, reading HA) are the user's: stop and ask.
- First flash over USB (`--device /dev/cu.usbmodemXXXX`, board in download mode: hold BOOT, tap RESET, release BOOT); afterwards `esphome run` uses OTA.
- Commit after every task, subject prefixed `waveshare:` (docs: `waveshare docs:`). Do not push unless asked.

## File Structure

```
waveshare/esp32-s3-touch-lcd-2/
  README.md                        board facts, HA setup, how to add a tile   (Task 8 finishes it)
  .gitignore                       .esphome/, esphome/secrets.yaml, *.bin
  esphome/secrets.yaml.example     template
  esphome/secrets.yaml             real values, never committed             (Task 1)
  esphome/lcd2.yaml                the device configuration                   (Tasks 2–7)
  docs/                            photos of the two pages                    (Task 8)
  docs/superpowers/{specs,plans}/  this spec and plan
```

---

### Task 1: Toolchain and secrets

**Files:**
- Create: `waveshare/esp32-s3-touch-lcd-2/esphome/secrets.yaml` (git-ignored)

**Interfaces:**
- Produces: a working `esphome` binary (Python 3.12 venv) and filled secrets for every later task.

- [ ] **Step 1: Python 3.12 and ESPHome in a venv**

```bash
brew install python@3.12
/usr/local/opt/python@3.12/bin/python3.12 -m venv ~/.venvs/esphome
~/.venvs/esphome/bin/pip install --upgrade pip esphome
~/.venvs/esphome/bin/esphome version
```

Expected: `Version: 2025.x.y`. If the version is older than 2025.5, `mipi_spi` may be missing; upgrade with `pip install -U esphome`.

- [ ] **Step 2: Secrets**

```bash
cd waveshare/esp32-s3-touch-lcd-2/esphome && cp secrets.yaml.example secrets.yaml
openssl rand -base64 32   # -> api_encryption_key
openssl rand -hex 16      # -> ota_password
```

Ask the user for the Wi-Fi SSID/password (2.4 GHz) and to paste them plus the two generated values into `secrets.yaml`. `ap_password` is any 8+ character string.

- [ ] **Step 3: Verify nothing secret is tracked**

```bash
git -C /Users/george.babanau/repos/embedded check-ignore -v waveshare/esp32-s3-touch-lcd-2/esphome/secrets.yaml
```

Expected: the `.gitignore` rule is printed. Nothing to commit in this task (the scaffold was committed with the plan).

---

### Task 2: Base firmware — boots, joins Wi-Fi, appears in Home Assistant

**Files:**
- Create: `waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`

**Interfaces:**
- Produces: `esphome:` node `lcd2`, `api:` with encryption, `ota:`, `wifi:` + fallback AP, `logger:` on native USB, `time: ha_time`. Every later task appends to this file.

- [ ] **Step 1: Write the base configuration**

```yaml
# Waveshare ESP32-S3-Touch-LCD-2 — Home Assistant touch dashboard (ESPHome, native API).
# Pins from Waveshare's demos: LCD SPI 38/39/40, CS 45, DC 42, BL 1; touch I2C 48/47 @0x15.
substitutions:
  name: lcd2
  friendly_name: "LCD2 Dashboard"

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  flash_size: 16MB
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

logger:
  level: INFO
  hardware_uart: USB_SERIAL_JTAG

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "${friendly_name} Fallback"
    password: !secret ap_password

captive_portal:

time:
  - platform: homeassistant
    id: ha_time

sensor:
  - platform: wifi_signal
    name: "WiFi Signal"
    update_interval: 60s
```

- [ ] **Step 2: Validate and compile**

```bash
esphome config waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
esphome compile waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Expected: config prints without errors; the first compile downloads the esp-idf toolchain (several minutes) and ends with `Successfully created esp32 image`.

- [ ] **Step 3: Hardware checkpoint (user) — first flash over USB**

Ask the user to connect the board over USB-C, put it in download mode (hold BOOT, tap RESET, release BOOT), then:

```bash
ls /dev/cu.usbmodem*
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml --device /dev/cu.usbmodemXXXX
```

Expected: upload, reset, then log lines `[wifi] ... Connected` and `[api] ... Address: lcd2.local`. In Home Assistant, Settings → Devices & services shows "Discovered: lcd2" → Configure → paste `api_encryption_key`. Then open the ESPHome integration entry for the device → **enable "Allow the device to perform Home Assistant actions"** (needed from Task 6 on). The `WiFi Signal` sensor appears in HA.

- [ ] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: ESPHome base config for ESP32-S3-Touch-LCD-2 (wifi, api, ota, usb logging)"
```

---

### Task 3: Display and backlight

**Files:**
- Modify: `esphome/lcd2.yaml` — add `spi:`, `output:`, `light:`, `display:` blocks

**Interfaces:**
- Produces: `display: lcd` (mipi_spi ST7789V 240×320), `light: backlight` (HA entity), `output: backlight_pwm`.

- [ ] **Step 1: Append the display blocks with a test pattern**

```yaml
spi:
  id: spi_lcd
  clk_pin: GPIO39
  mosi_pin: GPIO38
  miso_pin: GPIO40

output:
  - platform: ledc
    id: backlight_pwm
    pin: GPIO1
    frequency: 1000 Hz

light:
  - platform: monochromatic
    id: backlight
    name: "Backlight"
    output: backlight_pwm
    restore_mode: ALWAYS_ON
    default_transition_length: 0s

display:
  - platform: mipi_spi
    id: lcd
    model: ST7789V
    dimensions:
      width: 240
      height: 320
    spi_id: spi_lcd
    cs_pin: GPIO45
    dc_pin: GPIO42
    data_rate: 40MHz
    invert_colors: true      # IPS panel: Waveshare's demo constructs Arduino_ST7789 with ips=true
    color_order: bgr
    rotation: 0
    update_interval: 1s      # test pattern only; Task 4 switches to LVGL (update_interval: never)
    lambda: |-
      it.fill(Color::BLACK);
      it.filled_rectangle(0, 0, 120, 80, Color(255, 0, 0));       // top-left RED
      it.filled_rectangle(120, 0, 120, 80, Color(0, 255, 0));     // top-right GREEN
      it.filled_rectangle(0, 240, 120, 80, Color(0, 0, 255));     // bottom-left BLUE
      it.filled_rectangle(120, 240, 120, 80, Color(255, 255, 255)); // bottom-right WHITE
      it.rectangle(0, 0, 240, 320, Color(255, 255, 0));            // yellow 1-px border
```

- [ ] **Step 2: Validate, compile, flash (OTA now)**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

(`run` = compile + upload; choose the OTA target when asked.)

- [ ] **Step 3: Hardware checkpoint (user)**

Expected with the USB-C connector at the bottom: red top-left, green top-right, blue bottom-left, white bottom-right, a yellow border touching all four edges, black elsewhere.
- Red and blue swapped → `color_order: rgb`.
- Colours look like a photo negative (black background shows white) → `invert_colors: false`.
- Border not reaching an edge / a strip of noise → the panel offset differs: add `offset_width: 0` / `offset_height: 0` adjustments, or fall back to the legacy platform:

```yaml
display:
  - platform: st7789v
    model: Custom
    width: 240
    height: 320
    offset_width: 0
    offset_height: 0
    cs_pin: GPIO45
    dc_pin: GPIO42
    backlight_pin: no
    spi_id: spi_lcd
    rotation: 0
```

- Pattern rotated → change `rotation` (0/90/180/270) until the USB connector is at the bottom.
- The `Backlight` light entity in HA dims the panel from 0 to 100 %.

- [ ] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: ST7789 display over mipi_spi with PWM backlight and colour test pattern"
```

---

### Task 4: Touch and LVGL

**Files:**
- Modify: `esphome/lcd2.yaml` — add `i2c:`, `touchscreen:`, `lvgl:`; change the display to LVGL mode

**Interfaces:**
- Produces: `touchscreen: touch` (cst816), `lvgl:` with `page_test`. Later tasks replace `page_test` with the real pages.

- [ ] **Step 1: Switch the display to LVGL mode**

In the `display:` block delete `update_interval: 1s` and the whole `lambda:`; add:

```yaml
    auto_clear_enabled: false
    update_interval: never
```

- [ ] **Step 2: Append touch and a test page**

```yaml
i2c:
  id: i2c_tp
  sda: GPIO48
  scl: GPIO47
  frequency: 400kHz

touchscreen:
  - platform: cst816
    id: touch
    i2c_id: i2c_tp
    address: 0x15
    display: lcd
    update_interval: 50ms
    on_touch:
      - lambda: 'ESP_LOGI("touch", "x=%d y=%d", touch.x, touch.y);'

lvgl:
  displays: [lcd]
  touchscreens: [touch]
  buffer_size: 100%
  default_font: montserrat_20
  pages:
    - id: page_test
      bg_color: 0x000000
      widgets:
        - label:
            id: lbl_test
            align: CENTER
            text_color: 0xFFFFFF
            text: "Touch me"
        - button:
            id: btn_test
            x: 70
            y: 220
            width: 100
            height: 50
            widgets:
              - label:
                  align: CENTER
                  text: "Tap"
            on_click:
              - lvgl.label.update:
                  id: lbl_test
                  text: "Tapped!"
```

- [ ] **Step 3: Validate, compile, flash**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Then keep the log open: `esphome logs waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`.

- [ ] **Step 4: Hardware checkpoint (user)**

Expected: "Touch me" centred, a "Tap" button near the bottom. Touching logs `x= y=` with x growing left→right (0..239) and y top→bottom (0..319). Tapping the button changes the label to "Tapped!".
- Log shows `Failed to read chip id` / no touches → add `skip_probe: true` under the touchscreen.
- Coordinates mirrored or swapped → add under the touchscreen:

```yaml
    transform:
      mirror_x: false
      mirror_y: false
      swap_xy: false
```

and flip the one that fixes it (mirror_x if x runs right→left, mirror_y if y runs bottom→top, swap_xy if a horizontal finger move changes y).

- [ ] **Step 5: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: CST816 touch and LVGL test page"
```

---

### Task 5: Data tiles page with live Home Assistant values

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:` (tile entities), `sensor:` imports, replace `page_test` with `page_home`, add header and nav bar

**Interfaces:**
- Consumes: `ha_time`, `lcd`, `touch`.
- Produces: LVGL ids `lbl_time`, `lbl_link`, `tile1_value`..`tile4_value`, `page_home`, `page_controls` (placeholder until Task 6), `nav_bar` pattern.

- [ ] **Step 1: Substitutions**

Add under `substitutions:` (the user replaces the example entity IDs with real ones; formats are printf with the unit baked in):

```yaml
  tile1_entity: sensor.living_room_temperature
  tile1_name: "Living"
  tile1_format: "%.1f °C"
  tile2_entity: sensor.outdoor_temperature
  tile2_name: "Outside"
  tile2_format: "%.1f °C"
  tile3_entity: sensor.house_power
  tile3_name: "Power"
  tile3_format: "%.0f W"
  tile4_entity: sensor.living_room_humidity
  tile4_name: "Humidity"
  tile4_format: "%.0f %%"
```

- [ ] **Step 2: Import the four sensors**

Append to the existing `sensor:` list (below `wifi_signal`):

```yaml
  - platform: homeassistant
    id: tile1
    entity_id: ${tile1_entity}
    on_value:
      - lvgl.label.update:
          id: tile1_value
          text:
            format: "${tile1_format}"
            args: [x]
  - platform: homeassistant
    id: tile2
    entity_id: ${tile2_entity}
    on_value:
      - lvgl.label.update:
          id: tile2_value
          text:
            format: "${tile2_format}"
            args: [x]
  - platform: homeassistant
    id: tile3
    entity_id: ${tile3_entity}
    on_value:
      - lvgl.label.update:
          id: tile3_value
          text:
            format: "${tile3_format}"
            args: [x]
  - platform: homeassistant
    id: tile4
    entity_id: ${tile4_entity}
    on_value:
      - lvgl.label.update:
          id: tile4_value
          text:
            format: "${tile4_format}"
            args: [x]
```

- [ ] **Step 3: Clock and link state**

Extend the existing blocks:

```yaml
time:
  - platform: homeassistant
    id: ha_time
    on_time:
      - seconds: 0
        then:
          - lvgl.label.update:
              id: lbl_time
              text: !lambda 'return id(ha_time).now().strftime("%H:%M");'

api:
  encryption:
    key: !secret api_encryption_key
  on_client_connected:
    - lvgl.label.update:
        id: lbl_link
        text: "HA ●"
        text_color: 0x40C040
  on_client_disconnected:
    - lvgl.label.update:
        id: lbl_link
        text: "HA ○"
        text_color: 0xC04040
```

- [ ] **Step 4: Replace `page_test` with the home page and a controls placeholder**

Replace the whole `pages:` list under `lvgl:` with:

```yaml
  style_definitions:
    - id: tile_style
      bg_color: 0x1E2430
      bg_opa: COVER
      radius: 10
      border_width: 0
      pad_all: 6
    - id: nav_style
      bg_color: 0x2A3242
      radius: 8
  top_layer:
    widgets:
      - label:
          id: lbl_time
          x: 8
          y: 6
          text: "--:--"
          text_color: 0xFFFFFF
      - label:
          id: lbl_link
          x: 180
          y: 6
          text: "HA ○"
          text_color: 0xC04040
      - button:
          x: 8
          y: 288
          width: 108
          height: 28
          styles: nav_style
          widgets:
            - label:
                align: CENTER
                text: "Home"
          on_click:
            - lvgl.page.show: page_home
      - button:
          x: 124
          y: 288
          width: 108
          height: 28
          styles: nav_style
          widgets:
            - label:
                align: CENTER
                text: "Controls"
          on_click:
            - lvgl.page.show: page_controls
  pages:
    - id: page_home
      bg_color: 0x000000
      widgets:
        - obj:
            x: 8
            y: 36
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "${tile1_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: tile1_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
        - obj:
            x: 124
            y: 36
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "${tile2_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: tile2_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
        - obj:
            x: 8
            y: 162
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "${tile3_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: tile3_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
        - obj:
            x: 124
            y: 162
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "${tile4_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: tile4_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
    - id: page_controls
      bg_color: 0x000000
      widgets:
        - label:
            align: CENTER
            text: "Controls (Task 6)"
            text_color: 0xFFFFFF
```

Also add `montserrat_14` and `montserrat_28` to the fonts LVGL builds by listing them once anywhere they are used (ESPHome enables a built-in font when it appears in the config; nothing else to declare).

- [ ] **Step 5: Validate, compile, flash; checkpoint (user)**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Expected: header shows the clock within a minute and `HA ●` in green; the four tiles show the same values as the HA dashboard, updating when HA does; "Controls" shows the placeholder, "Home" returns. Long values overflow a tile? Shorten the `_format`.

- [ ] **Step 6: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: home page with four live HA tiles, clock and link state"
```

---

### Task 6: Controls page — toggle Home Assistant devices

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:` (switch entities), `binary_sensor:` imports, `page_controls` widgets

**Interfaces:**
- Consumes: `page_controls`, HA setting "Allow the device to perform Home Assistant actions" (Task 2 checkpoint).
- Produces: LVGL ids `sw1`..`sw4`; binary sensors `sw1_state`..`sw4_state`.

- [ ] **Step 1: Substitutions**

```yaml
  sw1_entity: switch.desk_lamp
  sw1_name: "Desk lamp"
  sw2_entity: light.living_room
  sw2_name: "Living light"
  sw3_entity: switch.fan
  sw3_name: "Fan"
  sw4_entity: input_boolean.guest_mode
  sw4_name: "Guest mode"
```

- [ ] **Step 2: State feedback from HA**

```yaml
binary_sensor:
  - platform: homeassistant
    id: sw1_state
    entity_id: ${sw1_entity}
    on_state:
      - lvgl.widget.update:
          id: sw1
          state:
            checked: !lambda 'return x;'
  - platform: homeassistant
    id: sw2_state
    entity_id: ${sw2_entity}
    on_state:
      - lvgl.widget.update:
          id: sw2
          state:
            checked: !lambda 'return x;'
  - platform: homeassistant
    id: sw3_state
    entity_id: ${sw3_entity}
    on_state:
      - lvgl.widget.update:
          id: sw3
          state:
            checked: !lambda 'return x;'
  - platform: homeassistant
    id: sw4_state
    entity_id: ${sw4_entity}
    on_state:
      - lvgl.widget.update:
          id: sw4
          state:
            checked: !lambda 'return x;'
```

- [ ] **Step 3: The controls page**

Replace the `page_controls` entry with four rows; one row shown, the others identical with `sw2..sw4`, `y: 106 / 168 / 230` and their substitutions:

```yaml
    - id: page_controls
      bg_color: 0x000000
      widgets:
        - obj:
            x: 8
            y: 44
            width: 224
            height: 54
            styles: tile_style
            widgets:
              - label: { text: "${sw1_name}", text_color: 0xFFFFFF, align: LEFT_MID }
              - switch:
                  id: sw1
                  align: RIGHT_MID
                  width: 56
                  height: 30
                  on_value:
                    - if:
                        condition:
                          lambda: 'return x;'
                        then:
                          - homeassistant.action:
                              action: homeassistant.turn_on
                              data:
                                entity_id: ${sw1_entity}
                        else:
                          - homeassistant.action:
                              action: homeassistant.turn_off
                              data:
                                entity_id: ${sw1_entity}
```

`homeassistant.turn_on/turn_off` work for switches, lights, fans and input_booleans, so the same block serves any entity domain.

- [ ] **Step 4: Validate, compile, flash; checkpoint (user)**

Expected: each switch shows the current HA state at boot; flipping one toggles the real device within a second; toggling the device from HA moves the switch on the panel. If nothing happens and the log says `Action ... denied` or shows no call, the "Allow the device to perform Home Assistant actions" setting is off (Task 2 checkpoint).

- [ ] **Step 5: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: controls page toggling HA devices with state feedback"
```

---

### Task 7: Idle dimming, touch wake, battery voltage

**Files:**
- Modify: `esphome/lcd2.yaml` — `lvgl.on_idle`, touchscreen `on_touch`, `sensor:` battery

**Interfaces:**
- Consumes: `backlight`, `touch`, `lvgl`.
- Produces: HA sensor `Battery Voltage`; idle behaviour.

- [ ] **Step 1: Idle and wake**

Under `lvgl:` add:

```yaml
  on_idle:
    - timeout: 60s
      then:
        - light.turn_off: backlight
        - lvgl.pause:
```

Replace the touchscreen `on_touch` logging with:

```yaml
    on_touch:
      - if:
          condition: lvgl.is_paused
          then:
            - lvgl.resume:
            - lvgl.widget.redraw:
            - light.turn_on:
                id: backlight
                brightness: 100%
```

- [ ] **Step 2: Battery voltage**

Append to `sensor:`:

```yaml
  - platform: adc
    pin: GPIO5
    name: "Battery Voltage"
    id: battery_v
    attenuation: 12db
    update_interval: 60s
    filters:
      - multiply: 3.0        # Waveshare demo: V = 3.3/4096 * adc * 3 (1:3 divider)
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
    unit_of_measurement: "V"
    accuracy_decimals: 2
```

- [ ] **Step 3: Validate, compile, flash; checkpoint (user)**

Expected: after 60 s untouched the screen goes dark; one touch brings it back with the UI intact (no ghosting); `Battery Voltage` in HA reads ~4.9–5.1 V on USB without a battery, or the cell voltage (3.3–4.2 V) with one; compare with a multimeter on the MX1.25 pads and adjust `multiply` if off by more than 0.1 V.

- [ ] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: idle backlight off with touch wake, battery voltage sensor"
```

---

### Task 8: Documentation and finish

**Files:**
- Modify: `waveshare/esp32-s3-touch-lcd-2/README.md`
- Create: `waveshare/esp32-s3-touch-lcd-2/docs/page-home.jpg`, `docs/page-controls.jpg` (user photos, EXIF-stripped)
- Modify: `README.md` at the repo root (one line listing the project)

- [ ] **Step 1: README**

Replace the "Status: planned" paragraph with: what it does, the two pages with photos, HA setup (ESPHome integration, API key, "Allow the device to perform Home Assistant actions"), build/flash commands (venv path, `esphome run`, first-flash download mode), how to add a tile or a switch (the three places), the idle behaviour, and a troubleshooting list (colour order, invert, touch transform, skip_probe). Keep the board pin table.

- [ ] **Step 2: Repo README**

Add a bullet for `waveshare/esp32-s3-touch-lcd-2` next to the existing projects.

- [ ] **Step 3: Commit and finish**

```bash
git add waveshare/esp32-s3-touch-lcd-2 README.md
git commit -m "waveshare docs: ESP32-S3-Touch-LCD-2 dashboard README, photos, repo index"
```

Then use superpowers:finishing-a-development-branch (merge / PR / keep).

---

## Later ideas (not tasks)

- Material Design Icons font (`font: - file: gfonts://...` or a local `.ttf`) for tile icons and the header.
- QMI8658 IMU via an external component for auto-rotate or a tap-to-wake.
- TF card via the `sd_mmc_card` external component (shares SPI CS 41) for logging without HA.
- Camera: `esp32_camera` with the DVP pins in §2 of the spec, streaming to HA.

## Self-review notes

- Spec §1 scope → Tasks 2–7; §2 pins → Global Constraints + Tasks 3/4/7; §3 architecture → Tasks 2, 5, 6; §4 UI → Tasks 5–7; §5 substitutions → Tasks 5, 6; §6 HA-side → Task 2 checkpoint, Task 6 checkpoint; §7 verification → each checkpoint; §8 fallbacks → Task 3/4 checkpoints.
- Names: `lcd`, `touch`, `backlight`, `ha_time` (2–3) used in 4–7; `page_home/page_controls`, `tile1_value..tile4_value`, `lbl_time`, `lbl_link` (5) used in 6–7; `sw1..sw4` and `sw1_state..` (6). The `mipi_spi` model name and the `homeassistant.action` `action:` key are current-release syntax; if `esphome config` rejects either, the legacy `st7789v` snippet (Task 3) and `service:` (older releases) are the fallbacks.

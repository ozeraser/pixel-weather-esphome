# Pixel Weather for ESPHome

An ESPHome external component that turns an **iDotMatrix 16x16 pixel panel** into a weather display for
**Home Assistant**. An ESP32 renders animated weather screens, encodes them as a GIF and uploads them to the
panel over Bluetooth LE.

- **Weather screens:** current temperature with an animated condition icon, today's high and low, and a
  16-hour rain strip. A UV screen is added when the UV index is 3 or more.
- **Lightning warning:** when a weather station reports a strike nearby, the panel shows a storm animation
  with the distance and **sounds its buzzer**. That uses the panel's alarm; see
  [docs/IDOTMATRIX_ALARM_PROTOCOL.md](docs/IDOTMATRIX_ALARM_PROTOCOL.md).
- **Message:** a short scrolling text (up to 24 characters) from a text entity.
- **Home Assistant entities:**
  - Status: panel connected, last upload result and time, upload failures, what's showing, lightning warning.
  - Controls: brightness, panel on/off, Refresh now, Test beep, Lightning beep on/off, beep length, warning distance and time.
- **Bluetooth:** the ESP32 connects to the panel only while uploading, so the phone app can still use it in between.

## Hardware
- An iDotMatrix 16x16 panel. Only this size has been tested.
- An ESP32 with Bluetooth LE. Developed on an **ESP32-C3** (ESP-IDF framework).
  - Some small C3 boards fail to join Wi-Fi ("Auth Expired") at full transmit power.
  - The example config uses `output_power: 8.5dB` to avoid that.

## Setup
1. Copy `components/`, `pixel-weather.yaml` and `secrets.yaml.example` into your ESPHome config folder.
   Rename `secrets.yaml.example` to `secrets.yaml` and fill it in. If you already have a `secrets.yaml`,
   add these entries to it instead.
2. In `pixel-weather.yaml`:
   - Set `panel_mac` to your panel's Bluetooth address. The panel advertises as `IDM-xxxxxx`; any BLE scanner app will show the address.
   - Set the `board:` to match your ESP32.
3. Copy `packages/pixel_weather.yaml` into Home Assistant. It creates `sensor.pixel_weather_forecast`, with
   rain for the next 16 hours and the 24-hour high and low, from a `weather.*` entity's hourly forecast.
   It needs packages enabled in `configuration.yaml`:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
4. Change the entity IDs in both files to yours:
   - **Weather forecast:** `weather.home` (Met.no).
   - **Weather station:** `sensor.gw2000c_*`, from an Ecowitt GW2000 (outdoor temperature, UV index, lightning distance and last strike time).
   - The UV and lightning sensors are optional.
5. Install from ESPHome, then adopt the device in Home Assistant.

## Files
| File | What it is |
|---|---|
| `components/pixel_weather/pw_core.h`, `icons.h` | Renderer, fonts, icons, GIF encoder and GIF upload packets. Plain C++ with no ESPHome dependencies. |
| `components/pixel_weather/pw_extra.h` | HA condition mapping, the UV, lightning and message screens, and the alarm (beep) packets. |
| `components/pixel_weather/pixel_weather.*` | The ESPHome component: Bluetooth connection, upload state machine and lightning check. |
| `packages/pixel_weather.yaml` | The Home Assistant forecast sensor. |
| `docs/IDOTMATRIX_ALARM_PROTOCOL.md` | How to make the panel beep: alarm and buzzer packets, and the gotchas. |

## Notes
- **Brand:** not affiliated with iDotMatrix. The protocol details come from community reverse-engineering
  and from checking the official app's behaviour, for interoperability.
- **Beep timing:** the beep goes off at the next whole minute at least 10 seconds away, because panel alarms
  only have minute precision.
- **Alarm slot:** the beep uses alarm **slot 0**, which overwrites the first alarm set in the phone app.
  Slot 9 did not beep; slots 1–8 haven't been tested.

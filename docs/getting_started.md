# Getting started - from bare board to a calibrated sensor

This is the long version of the quick start in the
[repository README](../README.md). All commands are run from the ESPHome project
directory, [`../esphome/`](../esphome/).

## 1. Install ESPHome

* **Home Assistant add-on** (easiest): Settings -> Add-ons -> ESPHome Device
  Builder. It builds and flashes over the network, and the board is offered to
  Home Assistant as soon as it is online.
* **CLI**: `pipx install esphome` (or `pip install esphome`) in a Python 3.12
  environment, plus the USB driver of your devkit (CP2102 or CH340).

## 2. Create `secrets.yaml`

`esphome/secrets.yaml` is not versioned - create it next to `config.yaml` with
the keys used by [`../esphome/packages/wifi.yaml`](../esphome/packages/wifi.yaml):

```yaml
wifi_ssid_1: "your-iot-ssid"
wifi_password_1: "your-iot-password"
wifi_ssid_2: "your-main-ssid"
wifi_password_2: "your-main-password"
wifi_ssid_3: "your-guest-ssid"
wifi_password_3: "your-guest-password"
fallback_hotspot_password: "the-ap-password"
```

If no network is reachable the board opens the access point `<name> Fallback`
(`name` comes from the substitutions below) with `fallback_hotspot_password`, so
you can always reconnect and fix the credentials.

## 3. Set the substitutions

Top of [`../esphome/config.yaml`](../esphome/config.yaml):

| Key | Default | Meaning |
|---|---|---|
| `name` | `h2_sensor` | hostname and entity-id prefix - no spaces or accents |
| `friendly_name` | `H2 sensor board` | the name shown in Home Assistant |
| `board_type` | `az-delivery-devkit-v4` | `nodemcu-32s` and `esp-wrover-kit` also work |
| `TZ` | `Europe/Zurich` | IANA timezone, used by SNTP and the weekly restart |

Wiring, in the package you include:

| Key | Default | Meaning |
|---|---|---|
| `mq8_pin` | `GPIO34` | ADC1 pin fed by the divider |
| `mq8_divider_r1` / `mq8_divider_r2` | `10.0` / `20.0` | divider resistors in kΩ (series / to ground) |
| `mq8_rl` | `10.0` | load resistor of the MQ-8 module in kΩ - measure it |
| `mics_pin` | `GPIO33` | MiCS-5524 analog input (through the divider) |
| `mics_divider_r1` / `mics_divider_r2` | `10.0` / `20.0` | same divider, for the MiCS module |
| `mics_enable_pin` | `GPIO4` | the module's `EN` pad (LOW = enabled); remove the key if unused |
| `mics_max_ppm` | `1000` | top of the vendor range for H2 |
| `mq8_i2c_sda` / `mq8_i2c_scl` | `GPIO21` / `GPIO22` | I2C pins of the optional SHT4x |
| `mq8_sht4x_address` | `0x44` | I2C address of the T/RH sensor |

## 4. Pick the packages

[`../esphome/config.yaml`](../esphome/config.yaml) already includes `wifi`,
`board`, `time`, `sensors_others`, `switch` and one MQ-8 package:

| Package | Sensor | When to use |
|---|---|---|
| `packages/mq8.yaml` | MQ-8 only | the default, no compensation |
| `packages/mq8_tc.yaml` | MQ-8 + SHT4x | *instead of* `mq8.yaml`, adds the temperature/humidity correction |
| `packages/mics5524.yaml` | MiCS-5524 | additive, when the trace sensor is wired |

Include **exactly one** of `mq8.yaml` / `mq8_tc.yaml`: both define `id: mq8`, and
two packages defining the same id are rejected. `mics5524.yaml` can be added on
top of either (it uses `id: mics`).

## 5. Wire it

* Module `VCC` -> the devkit's `5V` pin (the heater needs it, and the whole
  measurement scales with the supply), module `GND` -> ESP32 `GND`.
* `AO` / `A0` -> divider -> an **ADC1** pin (GPIO32-39). Never straight to a pin.
* Recommended divider: **10 kΩ in series, 20 kΩ to ground** -> ×1.5 -> 3.33 V at
  the pin for a 5 V output. To cover the whole range use 10k/10k (2.5 V) or an
  ADS1115 (`voltage_multiplier: 1.0`, `adc_input_max` / `adc_pin_max: 6.144`).
* MiCS-5524: `EN` -> the configured GPIO; most modules are enabled by a LOW level
  (`inverted: true`).
* Optional SHT4x: `VDD` 3.3 V, `GND`, `SDA` / `SCL` to the configured I2C pins
  (most breakouts already carry the pull-ups).

Details, caveats and placement: [`mq8_sensor_guide.md`](mq8_sensor_guide.md) and
[`mics5524_guide.md`](mics5524_guide.md).

## 6. Validate and flash

```bash
cd esphome
esphome config config.yaml        # fast validation, no hardware needed
esphome compile config.yaml       # full ESP-IDF build (the first one takes a while)
esphome run config.yaml           # flash over USB, later over OTA
```

* First **serial** flash: hold `BOOT` for 2-3 s while the connection initialises.
* After an **OTA** update: press `EN` (reset) once so the new firmware runs.
* Home Assistant discovers the board as soon as it is on the network (the API is
  enabled in `packages/board.yaml`).

## 7. First run, burn-in, calibration

1. **Burn-in**: a brand-new MQ-8 (and MiCS-5524) needs **24-48 h** before the
   readings are stable. Set `warmup_time: 24h` in the package while that runs:
   the sensor publishes `unknown` instead of a misleading ppm value.
2. **Clean air**: calibrate where there is no hydrogen - no alcohol, solvents or
   a recently used gas stove either - with ventilation running.
3. **Calibration is automatic** on the first boot, after `calibration.delay`
   (5 min MQ-8, 3 min MiCS) and never before `warmup_time` ends. The result is
   stored in flash (`persist: true`) and reused after every reboot:
   * MQ-8: `ratio_in_clean_air: 70` (RS/R0 in clean air), 50 samples;
   * MiCS-5524: the vendor clean-air reference (`x_air = VCC - V_AO`), 10 samples.
4. **Force a re-calibration** with an `on_boot` action in your own YAML:

   ```yaml
   esphome:
     on_boot:
       - delay: 5min
       - lambda: id(mq8).request_calibration();   # or id(mics).request_calibration();
   ```

5. **Pin the result** once you trust it: `r0: 0.2899` for the MQ-8 or
   `air_reference: <value>` for the MiCS, using the value printed in the log
   (`R0 = ... kOhm`, `air reference = ...`). A pinned value with no
   `calibration:` block is the deterministic setup for a room that is not always
   clean.
6. **Re-calibrate** after changing the wiring, the divider or `rl:`, and every
   few months - metal-oxide sensors drift.

Sanity check in clean air: RS/R0 ≈ 70 and ≈ 50 ppm on the MQ-8, ratio ≈ 1.0 on
the MiCS. Values far from that mean the divider ratio, `rl:` or the calibration
is wrong.

## 8. Next steps

* [`home_assistant_alerts.md`](home_assistant_alerts.md) - entities, thresholds,
  automations and operations.
* [`troubleshooting.md`](troubleshooting.md) - when a number does not add up.
* [`development.md`](development.md) - validate, test and lint commands.


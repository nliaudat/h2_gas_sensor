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
| `mq8_pin` | `GPIO39` | ADC1 pin fed by the divider - the pin of the PCB in `pcb/` |
| `mq8_divider_r1` / `mq8_divider_r2` | `10.0` / `20.0` | divider resistors in kΩ (series / to ground) |
| `mq8_rl` | `10.0` | load resistor of the MQ-8 module in kΩ - measure it |
| `mics_pin` | `GPIO36` | MiCS-5524 analog input (through the divider) |
| `mics_divider_r1` / `mics_divider_r2` | `10.0` / `20.0` | same divider, for the MiCS module |
| `mics_enable_pin` | `GPIO4` | the module's `EN` pad (LOW = enabled); remove the key if unused |
| `mics_max_ppm` | `1000` | top of the vendor range for H2 |
| `mq8_i2c_sda` / `mq8_i2c_scl` | `GPIO21` / `GPIO22` | I2C pins of the optional SHT4x example in `packages/mq8.yaml` |
| `mq8_sht4x_address` | `0x44` | I2C address of that SHT4x |
| `mq8_dht_pin` | `GPIO27` | 1-wire `DATA` pin of the optional DHT11/DHT22 example, any bidirectional GPIO |
| `mq8_dht_model` | `AUTO_DETECT` | `dht` model of that example (`AUTO_DETECT`, `DHT11`, `DHT22`, `DHT22_TYPE2`, `AM2302`, `RHT03`, `SI7021`, `AM2120`) |
| `alarm_buzzer_pin` / `alarm_led_pin` | `GPIO33` / `GPIO19` | local pre-alarm (`packages/alarm.yaml`): piezo output and SK6812 data line |
| `alarm_led_count` / `alarm_led_chipset` / `alarm_led_channel_colors` | `2` / `SK6812` / `GRB` | strip geometry and colour order (`GRBW` for RGBW LEDs) |
| `alarm_early_ppm` / `alarm_danger_ppm` | `4000` / `10000` | amber LEDs + beeps from 10 % of the LEL, buzzer silent from 25 % - see [`local_alarm.md`](local_alarm.md) |
| `alarm_beep_interval` / `alarm_rtttl` | `5s` / `two_short:...` | repeat interval and RTTTL melody of the pre-alarm beep |
| `alarm_led_refresh_interval` | `60s` | how often the LED state is re-asserted (self-heal of a stray command) |
| `mq8_update_interval` / `mq8_adc_update_interval` / `mq8_diag_interval` | `1s` / `1s` / `1s` | poll interval of the `H2 (MQ-8)` entity, of its raw `AO voltage` entity and the publish throttle of its RS / ratio / T-RH diagnostics |
| `mq8_trh_update_interval` | `60s` | poll interval of the two commented T/RH examples in `packages/mq8.yaml` |
| `mics_update_interval` / `mics_adc_update_interval` / `mics_diag_interval` | `1s` / `1s` / `1s` | the same three keys for the MiCS-5524 chain |
| `mics_extra_gas_update_interval` | `1s` | poll interval of the `CO` / `NH3` / `C2H5OH` / `CH4` views (the same analog signal) |
| `dht22_update_interval` | `60s` | poll interval of the ambient T/RH sensor (≥ 2 s, the 1-wire protocol) |
| `wifi_signal_update_interval` | `60s` | poll interval of the `WiFi Signal` diagnostic entity |

Every update interval is a substitution at the top of its package, so a chain can
be slowed down in one place (e.g. `mq8_diag_interval: 30s` in `config.yaml` when
the Home Assistant recorder matters).  The shipped defaults keep the gas entities
at 1 Hz - the MOX heaters run continuously - and only the ambient sensors at 60 s.
`never` is valid only on the two `*_adc_update_interval` keys: it switches the raw
voltage entity off, while the ppm value keeps being measured (the gas entry samples
the ADC itself).  The per-entity refresh table is in
[`home_assistant_alerts.md`](home_assistant_alerts.md).

## 4. Pick the packages

[`../esphome/config.yaml`](../esphome/config.yaml) already includes `wifi`,
`board`, `time`, `sensors_others`, `switch` and one MQ-8 package:

| Package | Sensor | When to use |
|---|---|---|
| `packages/mq8.yaml` | MQ-8 | always; the ambient T/RH sensor examples and the comment-only compensation keys are inside |
| `packages/dht22.yaml` | DHT22 (ambient T/RH) | when a DHT22 is wired: it links `temperature:`/`humidity:` into `id: mq8`, which selects the compensation |
| `packages/alarm.yaml` | Buzzer + 2 x SK6812 | when the local pre-alarm is wired: it merges into `id: mq8`, so it must stay *after* `mq8`; remove the include when there is no buzzer/LED strip |
| `packages/mics5524.yaml` | MiCS-5524 | additive, when the trace sensor is wired |

There is a single MQ-8 package: it defines `id: mq8` and links the ambient sensors
by id (`temperature:` / `humidity:`), so there is no per-sensor variant. **Linking
both ids selects the MQDataScience compensation** - either from
`packages/dht22.yaml` (the shipped `id: !extend mq8` fragment) or by uncommenting
the matching block inside `packages/mq8.yaml`; `correction_mode: none` is the
explicit opt-out. `mics5524.yaml` can be added on top of it (it uses `id: mics`).

## 5. Wire it

* Module `VCC` -> the devkit's `5V` pin (the heater needs it, and the whole
  measurement scales with the supply), module `GND` -> ESP32 `GND`.
* `AO` / `A0` -> divider -> an **ADC1** pin (GPIO32-39). Never straight to a pin.
* Using the PCB in [`../pcb/`](../pcb/) instead of a breadboard? Its schematic
  pairs the MQ-8 with `ADC-2` (GPIO39) and the MiCS-5524 with `ADC-1` (GPIO36) -
  the two defaults above - and its table is in [`hardware.md`](hardware.md). A
  breadboard on the older pins (MQ-8 on GPIO36, MiCS-5524 on GPIO39) needs the two
  overrides commented in [`../esphome/config.yaml`](../esphome/config.yaml), and
  any pin change needs a new clean-air calibration.
* Recommended divider: **10 kΩ in series, 20 kΩ to ground** -> ×1.5 -> 3.33 V at
  the pin for a 5 V output. To cover the whole range use 10k/10k (2.5 V) or an
  ADS1115 (`voltage_multiplier: 1.0`, `adc_input_max` / `adc_pin_max: 6.144`).
* MiCS-5524: `EN` -> the configured GPIO; most modules are enabled by a LOW level
  (`inverted: true`).
* Optional SHT4x (the I2C example in `packages/mq8.yaml`): `VDD` 3.3 V, `GND`,
  `SDA` / `SCL` to `mq8_i2c_sda` / `mq8_i2c_scl` (most breakouts already carry
  the pull-ups); uncomment the `i2c:` block and that sensor.
* Optional DHT11/DHT22 - the 1-wire example inside `packages/mq8.yaml`
  (`mq8_dht_pin`) or the shipped `packages/dht22.yaml` (`dht22_pin`): `VCC` 3.3 V,
  `GND`, `DATA` -> that pin plus a 4.7 kΩ - 10 kΩ pull-up to 3.3 V (bare
  3-pin sensors need it). Never route `DATA` to an input-only pin (GPIO34-39) -
  the 1-wire protocol drives the line - and do not power a module whose pull-up
  sits on 5 V from 5 V: the GPIO is not 5 V tolerant.

* Optional local pre-alarm (`packages/alarm.yaml`) - a **passive** piezo from
  `alarm_buzzer_pin` (GPIO33) to GND, plus 2 x SK6812 fed from 5 V with their
  data line on `alarm_led_pin` (GPIO19), a 100 - 500 Ω series resistor, a 10 kΩ
  pull-down and a bulk capacitor.  The wiring caveats (passive vs active buzzer,
  3.3 V data into a 5 V SK6812) and the state table are in
  [`local_alarm.md`](local_alarm.md).

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
4. **Force a re-calibration** with the `MQ-8 recalibrate` /
   `MiCS-5524 recalibrate` button in Home Assistant, or with an `on_boot` action
   in your own YAML:

   ```yaml
   esphome:
     on_boot:
       - delay: 5min
       - lambda: id(mq8).request_calibration();   # or id(mics).request_calibration()
   ```

   A request is deferred until `warmup_time` has elapsed and the value stays
   `unknown` while the calibration is pending or running - do not press the
   button while hydrogen (or alcohol) may be present.

5. **Pin the result** once you trust it: `r0: 0.2899` for the MQ-8 or
   `air_reference: <value>` for the MiCS, using the value printed in the log
   (`R0 = ... kOhm`, `air reference = ...`) and mirrored to the `MQ-8 logs` /
   `MiCS-5524 logs` text sensor. A pinned value with no `calibration:` block is
   the deterministic setup for a room that is not always clean.
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

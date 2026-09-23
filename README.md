# H2 gas sensor - battery-room hydrogen monitor (ESPHome)

ESPHome firmware for a **hydrogen monitor**: an ESP32 reads an **MQ-8** (and
optionally a **MiCS-5524**) metal-oxide gas sensor, converts the analog output
into a ppm value and publishes it to Home Assistant. Built for a nickel-iron
(Edison) battery room, where hydrogen is released near the end of every charge
cycle.

> **Not a certified gas detector.** These are cheap metal-oxide sensors: they are
> cross-sensitive to other reducing gases, spread ± 30 % between units, and the
> ppm values are estimates. Use them as a trend and an early warning, keep a
> certified detector for anything safety-relevant, and keep the alarm logic in
> Home Assistant where you can see and test it.

| | |
|---|---|
| Board | ESP32 devkit (`az-delivery-devkit-v4` by default), ESP-IDF framework |
| Sensors | MQ-8 (4 000 - 10 000 ppm band) + optional MiCS-5524 (100 - 1 000 ppm trace band) |
| Optional | SHT4x (I2C) for the temperature/humidity compensation |
| Firmware | [`esphome/`](esphome/) - entry point [`esphome/config.yaml`](esphome/config.yaml) |
| Documentation | [`docs/README.md`](docs/README.md) - index of every document |
| Licence | Apache-2.0 OR MIT - see [`LICENSE`](LICENSE) |

## What you need

| Part | Notes |
|---|---|
| ESP32 devkit | 5 V and 3.3 V pins + a USB cable; `az-delivery-devkit-v4`, `nodemcu-32s` and `esp-wrover-kit` are preconfigured |
| MQ-8 module (5 V breakout) | 5 V heater (~150 mA), analog `AO` output. Cheap boards often carry a 1 kΩ load resistor instead of the 10 kΩ of the datasheet circuit - measure it, see [`docs/mq8_sensor_guide.md`](docs/mq8_sensor_guide.md) |
| 2 resistors per sensor (e.g. 10 kΩ + 20 kΩ) | the voltage divider - **the ESP32 ADC pins are not 5 V tolerant** |
| MiCS-5524 module (optional) | 5 V, `A0` + `EN` pad, trace band 100 - 1 000 ppm |
| SHT4x / SHT3x (optional) | I2C temperature/humidity sensor for the `mq8_tc` package |

## Wiring at a glance

```
MQ-8 / MiCS-5524 at 5 V
   VCC -> 5V (module GND to the ESP32 GND)
   AO  -> [ 10k ] --> ADC1 pin (GPIO32-39)
                  |
                [ 20k ]
                  |
                 GND            ->  divider: {r1: 10.0, r2: 20.0}  (multiplier 1.5)
   EN (MiCS-5524 only) -> GPIO4, `inverted: true` (the module is enabled by LOW)
```

* Never wire `AO` / `A0` straight to a GPIO: the output can reach 5 V while the
  ADC absolute maximum is 3.6 V. The recommended 10k/20k divider maps 5 V to
  3.33 V - a hair above the 3.3 V recommended maximum, where the very top of the
  range can clip. Use 10k/10k (2.5 V) or an ADS1115 if that matters to you.
* Use an **ADC1** pin (GPIO32-39): ADC2 is unusable while Wi-Fi is active, and
  GPIO12 must not be used.

## Quick start

1. **Install ESPHome** (the Home Assistant add-on or the CLI) and connect the
   board over USB.
2. **Create `esphome/secrets.yaml`** with your Wi-Fi credentials and the fallback
   access-point password - the exact keys are listed in
   [`docs/getting_started.md`](docs/getting_started.md).
3. **Set your substitutions** in [`esphome/config.yaml`](esphome/config.yaml)
   (`name`, `friendly_name`, `board_type`, `TZ`) and include the packages you
   need (`mq8` *or* `mq8_tc`, optionally `mics5524`).
4. **Validate and flash**: `cd esphome && esphome run config.yaml`. Hold `BOOT`
   for 2-3 s for the first **serial** flash; after an OTA update press `EN` once
   to run the new firmware.
5. **Burn in, then calibrate**: 24-48 h for a new sensor (`warmup_time: 24h`),
   then a clean-air calibration - both are stored in flash.

Step by step, with the wiring details and the calibration workflow:
[`docs/getting_started.md`](docs/getting_started.md).

## What you get in Home Assistant

| Entity | Unit | Notes |
|---|---|---|
| `H2 (MQ-8)` | ppm | the alarm reference: 4 000 / 10 000 ppm |
| `H2 trace (MiCS-5524)` | ppm | early trend, 100 - 1 000 ppm band (optional package) |

Plus the diagnostics (`MQ-8 AO voltage`, `MQ-8 RS-R0 ratio`, `MQ-8 RS`,
`MiCS-5524 ratio`, `MQ-8 T/RH correction`, `ambient temperature/humidity`,
`WiFi Signal`) and a `restart` switch. Full entity list, thresholds and
paste-ready automations: [`docs/home_assistant_alerts.md`](docs/home_assistant_alerts.md).

```yaml
# Home Assistant (automations.yaml) - pre-alarm at the top of the MQ-8 range.
# Adjust the entity_id to the one your device created. The component clamps at
# 10 000 ppm and numeric_state's `above:` is exclusive, so a numeric_state
# trigger at 10 000 would never fire - hence the template trigger (>=).
- alias: "H2 pre-alarm (10 000 ppm)"
  trigger:
    - platform: template
      value_template: "{{ states('sensor.h2_sensor_board_h2_mq_8') | float(0) >= 10000 }}"
      for: "00:02:00"
  action:
    - service: notify.mobile_app_your_phone
      data:
        title: "H2 pre-alarm"
        message: ">= 10 000 ppm (25 % LEL) - ventilate and stop charging."
```

## Documentation

* [`docs/README.md`](docs/README.md) - index of every document.
* Setup: [`docs/getting_started.md`](docs/getting_started.md) - secrets, substitutions, packages, wiring, flashing, calibration.
* Usage: [`docs/home_assistant_alerts.md`](docs/home_assistant_alerts.md) - entities, thresholds, automations, operations.
* Hardware depth: [`docs/mq8_sensor_guide.md`](docs/mq8_sensor_guide.md), [`docs/mics5524_guide.md`](docs/mics5524_guide.md).
* Problems: [`docs/troubleshooting.md`](docs/troubleshooting.md).
* Firmware internals: [`esphome/readme.md`](esphome/readme.md) and the two component READMEs.
* Developing: [`docs/development.md`](docs/development.md) and the rule book [`.ai/instructions.md`](.ai/instructions.md).

## Licence

Dual-licensed under the **Apache License 2.0** *or* the **MIT License**, at your
option - see [`LICENSE`](LICENSE), [`LICENSE-APACHE`](LICENSE-APACHE) and
[`LICENSE-MIT`](LICENSE-MIT). Third-party material (MQUnifiedsensor,
MQDataScience, DFRobot_MICS, the vendored ESPHome CI script) keeps its own
licence - full table in
[`esphome/readme.md#licence`](esphome/readme.md#licence).

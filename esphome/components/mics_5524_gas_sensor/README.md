# mics_5524_gas_sensor

ESPHome sensor platform for the **MiCS-5524** MEMS multi-gas sensor (the small
5 V analog modules with `VCC` / `GND` / `A0` / `EN` pads, and the DFRobot
"Fermion" breakout they are cloned from).

Role in this project: the **trace / early-warning** device for hydrogen
(100 - 1000 ppm, i.e. 0.25 - 2.5 % of the LEL), while the MQ-8 covers the
4000 - 10000 ppm band - see [`docs/h2_thresholds.md`](../../../docs/h2_thresholds.md).

> The MiCS-5524 is **not** a safety-grade sensor. It has one analog output for
> several reducing gases (CO, H2, C2H5OH, NH3, CH4), it cannot tell them apart,
> individual sensors vary widely and the ESP32 ADC bottoms out around 75 mV
> (~7 ppm CO). Treat every value as an estimate.

## Two conversion models

| `conversion:` | Formula | Needs | Ships with |
|---|---|---|---|
| `dfrobot` (default) | `x = VCC - V_AO`, `ratio = x / x_air`, `ppm = (threshold - ratio) / gain` | a clean-air reference (calibrated in place) | all five gases (vendor thresholds/gains) |
| `datasheet` | `RS = (VCC * RL) / V_AO - RL`, `ppm = a * (RS / R0)^b` | `rl:`, a calibrated `r0:` | CO (`a = 6.3`, `b = -1.1`) |

The vendor model is what the DFRobot_MICS library (MIT) implements and what the
makerguides tutorial uses; because its reference is captured *in place*, it does
not depend on the board's load resistor or on the exact divider ratio - which is
why it is the default for the common unlabeled modules. The datasheet model is
the classic log-log fit (the same chain as the sibling `mq_gas_sensors`
component); every number and its provenance is documented in
[`docs/mics5524_conversion.md`](../../../docs/mics5524_conversion.md).

## Minimal configuration

```yaml
sensor:
  - platform: mics_5524_gas_sensor
    name: "H2 trace (MiCS-5524)"
    gas: H2
    pin: GPIO33                 # or: voltage: some_adc_or_ads1115_sensor
    voltage_multiplier: 1.5     # divider compensation (1.0 for a direct/ADS1115 hookup)
    vcc: 5.0
    warmup_time: 3min           # DFRobot's heater warm-up
    calibration:
      delay: 3min               # in clean air, after boot
      samples: 10
      persist: true
    enable_pin:                 # EN of the module (LOW = enabled)
      number: GPIO4
      inverted: true
```

`packages/mics5524.yaml` is a ready-to-use version (including the diagnostics and
the wiring notes).

## Options

| Key | Default | Description |
|---|---|---|
| `gas` | *required* | `H2`, `CO`, `NH3`, `C2H5OH`, `CH4` - the datasheet names `hydrogen`, `methane`, `ethanol`/`alcohol`, `ammonia`, `carbon monoxide` are accepted as aliases. |
| `conversion` | `dfrobot` | `dfrobot` (vendor model) or `datasheet` (`a * ratio^b`). |
| `voltage` | – | `id` of a voltage sampler (`adc`, `ads1115`, ...). Exactly one of `voltage`/`pin`. |
| `pin` | – | ADC pin owned by the component; a hidden internal `adc` sensor is generated with `adc_attenuation`/`adc_samples`. |
| `adc_attenuation` | `12db` (ESP32) | Only for `pin:`. |
| `adc_samples` | `1` | Only for `pin:`; ADC multisampling of the generated sensor. |
| `voltage_multiplier` | `1.0` | Scales the sampled voltage (divider compensation). |
| `vcc` | `5.0` | Module supply; the vendor model works on `VCC - V_AO`, the datasheet model needs it for `RS`. |
| `rl` | `10.0` | Load resistor of the board in kOhm - **datasheet model only** (the vendor model rejects it). |
| `a`, `b` | table | Datasheet coefficients - **datasheet model only**; required for every gas except CO. |
| `r0` | – | Fixed R0 in kOhm (datasheet model) - disables the automatic calibration. |
| `air_reference` | – | Fixed clean-air reference in volts (vendor model) - disables the automatic calibration. |
| `min_ppm`, `max_ppm` | `0` / table | Clamp of the published value (`max_ppm` defaults to the gas's vendor range: 1000 ppm for H2/CO, 500 for NH3/C2H5OH, 25000 for CH4). |
| `samples`, `sample_interval` | `4`, `20ms` | Averaging of the analog output per update. |
| `warmup_time` | `3min` | Heater warm-up; nothing is published before it elapses (use `24h` for the burn-in of a new sensor). |
| `enable_pin` | – | Pin that enables the module (DFRobot drives `EN` **low** to wake it - use `inverted: true`). |
| `calibration` | – | `delay:` (default `60s`), `samples:` (default `10`), `persist:` (default `true`). |
| `ratio_sensor`, `rs_sensor`, `voltage_sensor` | – | Optional diagnostic entities (the ratio of the active model, RS in kOhm for the datasheet model, the scaled AO voltage). |
| `update_interval` | `30s` | Normal polling interval. |

## Calibration

Both models need one clean-air number, captured after `max(calibration.delay,
warmup_time)` and (by default) stored in flash:

```
dfrobot    air reference = VCC - V_AO measured in clean air   (ratio is 1.0 there)
datasheet  R0            = RS measured in clean air           (ratio is 1.0 there)
```

* The reference is model specific - the flash entry is versioned with the
  conversion model, so switching `conversion:` cannot resurrect a stale value.
* The value is logged (`air reference = ...` / `R0 = ...`) and can be pinned with
  `air_reference:` / `r0:` to skip the calibration at boot.
* Re-calibrate after changing the wiring, the divider (`voltage_multiplier`) or
  the module: the stored reference is only valid for the same hardware.
* Do not calibrate while hydrogen may be present, and do not touch the sensor
  during the warm-up (the DFRobot example says exactly that).

## Behaviour and safety

* Invalid readings (AO <= 10 mV, open circuit, missing 5 V, no reference
  captured) publish `unknown` - never `0 ppm`, so a broken sensor cannot look
  like clean air. A one-time warning is logged.
* A `0 ppm` reading is only published when the model says "not detected" (the
  vendor ratio is above the gas threshold) - i.e. a *valid* clean-air reading.
  Note that the vendor's own minimum applies: H2/CO report 0 below 1 ppm,
  NH3/C2H5OH below 10 ppm, CH4 below 1000 ppm.
* `min_ppm`/`max_ppm` clamp the published value on top of the model's own range.
* The EN pin is driven once at setup (enabled) and never toggled at runtime:
  the vendor warm-up *is* a calibration, so duty-cycling would invalidate the
  reference.
* Two entries on the same sensor (e.g. H2 and CO) are technically possible, but
  they see the same gas mixture with different calibrations - the sensor has no
  selectivity.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| state stays `unknown`, `no air reference` warning | no `calibration:` block and no `air_reference:`/`r0:` |
| `analog output reads 0.0000 V` warning | AO not wired, module not powered from 5 V, EN not enabled (or the wrong polarity) |
| ratio stuck near 1.0 | sensor still warming up, or a gas-free environment (that is the baseline) |
| values jump around | ESP32 ADC noise - raise `samples`, add an RC filter, or use an `ads1115` (16 bit, gain 6.144 V covers the 0-5 V output without a divider) |
| ppm far too high | wrong `conversion` for the hardware (the vendor model expects the module's analog front-end), or a reference captured while gas was present |
| ppm always 0 | the vendor thresholds sit close to the clean-air ratio; re-calibrate in really clean air and check the ratio diagnostic |

## Tests

The math lives in `mq_math`-style isolation in `mics_math.h` (no ESPHome/ESP-IDF
dependency) and is covered by a host test:

```bash
cd esphome/tests
g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test
./mics_math_test
```

Validation helpers (no hardware needed):

```bash
esphome config tests/test_mics.yaml          # both models, diagnostics, negative cases
esphome config tests/test_mics_package.yaml  # the shipped package
```

## Credits

* Vendor model, thresholds, gains and ranges - `DFRobot_MICS` (MIT,
  `DFRobot/DFRobot_MICS`), `getGasData()` and the per-gas helpers.
* Datasheet power law for CO and the `RS = (VCC * RL) / V_AO - RL` chain - the
  Home Assistant community thread *"CO sensor - MICS-5524 or MICS-6814"*
  (posts 8, 11 and 13), which also documents the ADS1115 front-end.
* Hardware and usage tutorial - <https://www.makerguides.com/fermion-mems-multi-gas-sensor-mics-5524-with-arduino/>.


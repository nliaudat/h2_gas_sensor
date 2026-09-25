# MQ-8 sensor guide — hardware, wiring and calibration

Component reference: [`../esphome/components/mq_gas_sensors/README.md`](../esphome/components/mq_gas_sensors/README.md).
Curve and formula details: [`mq8_h2_curve.md`](mq8_h2_curve.md).

## Bill of materials

| Part | Notes |
|---|---|
| ESP32 devkit (az-delivery-devkit-v4 / nodemcu-32s) | ESP-IDF framework, see `esphome/packages/board.yaml` |
| MQ-8 module (breakout with LM393) | 5 V supply, analog output `AO` (and an unused digital `DO`) |
| 2 resistors for the divider (e.g. 10 kΩ + 20 kΩ) | protects the ADC from the 5 V swing |
| Temperature/humidity sensor (SHT4x / SHT3x on I2C, or a 1-wire DHT11) | only for the optional T/RH compensation: uncomment the matching example in `packages/mq8.yaml` |

## Power

* `VCC` of the MQ-8 module is **5 V** (heater ≈ 150 mA average). Take it from the
  devkit's `5V` pin, not from `3V3` — the sensor's RS and therefore the whole
  measurement scale with the heater supply.
* `GND` of the module and of the ESP32 must be the same node.

## Analog output and the ADC

The `AO` output swings between 0 V and `VCC` (5 V), while the ESP32 ADC stops
measuring correctly around 3.1-3.3 V with `attenuation: 12db`. Feed `AO` through a
divider - the recommended values are **10 kOhm / 20 kOhm**:

```
AO ──[ 10k ]──┬──> GPIO34 (ADC1_CH6)
              │
            [ 20k ]
              │
             GND          ratio = 20/(10+20) = 2/3  ->  divider: {r1: 10.0, r2: 20.0}
```

* Use an **ADC1** pin (GPIO32…GPIO39). ADC2 is unusable while Wi-Fi is active.
* `divider: {r1, r2}` (kOhm) is the documented way and derives
  `voltage_multiplier = (r1 + r2) / r2` = **1.5** for 10k/20k; the raw
  `voltage_multiplier` key stays available (1.0 for an ADS1115 or a directly
  connected 3.3 V sensor).
* **The ESP32 ADC pins are not 5 V tolerant.** The absolute maximum is VDD + 0.3 V
  (3.6 V), so never wire `AO` straight to a GPIO and never rely on the series
  resistor alone. With 10k/20k a 5 V output reaches **3.33 V** - a hair above the
  ESP32's 3.3 V recommended maximum, where the top of the range can read
  non-linearly; that is why the packages declare `adc_input_max: 3.33`. Use
  10k/10k (5 V -> 2.5 V) if the top of the AO range matters to you, or an ADS1115.
* The component enforces this: the configuration is **rejected at compile time**
  when `vcc / voltage_multiplier` exceeds `adc_pin_max` (3.6 V by default), and a
  warning is logged when it exceeds `adc_input_max` (3.3 V).
* GPIO34/35/36/39 are input-only, which is fine (and they are not strapping
  pins). Do not use GPIO12.

## The load resistor `RL`

Cheap breakout boards often carry **1 kΩ** instead of the 10 kΩ of the
datasheet circuit. Measure the resistor between `AO` and `GND` with the module
unpowered and set `rl:` accordingly — a wrong `RL` scales RS (and the ppm value)
directly:

```
RS = (VCC × RL) / V_AO − RL
```

## First start and burn-in

1. Power the module and let it run. A brand-new MQ-8 needs **24–48 h** of
   burn-in before the reading is stable; model that with
   `warmup_time: 24h` (the component publishes `unknown` during that window).
2. After the burn-in, calibrate `R0` in **clean air** (no hydrogen, no alcohol
   vapour, ventilation running):

   ```yaml
   calibration:
     ratio_in_clean_air: 70   # RS/R0 in clean air, MQ-8 datasheet
     delay: 5min              # wait for boot + heater stabilisation
     samples: 50
     persist: true            # store R0 in flash, reuse after a reboot
   ```

   The log prints the result: `'MQ-8 H2': R0 = 0.2899 kOhm (50/50 samples valid),
   stored in flash`.
3. Optionally pin the value (`r0: 0.2899`) and drop the `calibration:` block.
   That is the more deterministic setup for a battery room, where the air is
   *not* clean when hydrogen is present.
4. Re-calibrate every few months (MQ sensors drift): press the *MQ-8 recalibrate*
   button in Home Assistant, call `id(mq8).request_calibration()` in clean air,
   or flash a new `r0:`. A request is deferred until `warmup_time` has elapsed
   and the state stays `unknown` while the calibration is pending or running, so
   a press cannot capture an unstable RS.

Sanity check of the calibration: in clean air the component must report
RS/R0 ≈ 70 (the value of `ratio_in_clean_air`) and ≈ 50 ppm. If RS/R0 in clean
air is far off 70, the divider ratio or `rl:` is wrong.

## Placement

* Hydrogen is lighter than air: mount the sensor at the **highest point** of the
  room or enclosure.
* Keep it away from the cell vents (where the gas is still warm and humid) and
  from draughts that dilute the sample.
* Ambient temperature/humidity compensation (see
  [`temperature_humidity_correction.md`](temperature_humidity_correction.md))
  only helps if the T/RH sensor measures the air the MQ-8 sees.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `R0 unknown`, state stays `unknown` | no `r0:` and no `calibration:`, or the calibration has not run yet |
| warning `analog output reads 0.0000 V` | AO not connected, module not powered from 5 V, wrong divider |
| ppm far too low / nearly flat | `rl:` does not match the module, or `voltage_multiplier` is missing |
| ppm pinned at `max_ppm` (10 000) | wrong `a`/`b` for the gas, or `ratio_mode` mismatch |
| values drift over weeks | normal sensor drift — re-calibrate in clean air |
| correction factor stuck at 1.0000 | no T/RH sensor is linked (`temperature:`/`humidity:` on the MQ-8 entry), the T/RH sensor has no state yet (see the logs), or `correction_mode: none` was written |

`esphome logs config.yaml` (or the web log) prints every reading as
`V=… V, RS=… kOhm, ratio=… (correction=…) -> … ppm` at `DEBUG` level; the
`mq_gas_sensors` tag is pinned at `INFO` in
[`../esphome/config.yaml`](../esphome/config.yaml) (to mute the 30 s line), put
it back to `DEBUG` under `logger.logs` to read the chain again. The same lines -
plus the calibration messages and warnings - are mirrored to the `MQ-8 logs` text
sensor (`log_sensor:` in `packages/mq8.yaml`), so the chain is also readable from
Home Assistant.

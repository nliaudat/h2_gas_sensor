# MiCS-5524 guide - hardware, wiring and calibration

Component reference:
[`../esphome/components/mics_5524_gas_sensor/README.md`](../esphome/components/mics_5524_gas_sensor/README.md).
Formulas and provenance: [`mics5524_conversion.md`](mics5524_conversion.md).

## What it is (and is not)

A MEMS metal-oxide sensor with a **single analog output** for several reducing
gases. Sold as a small module with four pads: `VCC`, `GND`, `A0` (analog output)
and `EN` (enable, sometimes `PD`).

* **Is**: a cheap, sensitive *trend* sensor for the trace band (100 - 1000 ppm of
  H2 with this component's H2 curve) - exactly the early-warning role this project
  wants in front of the MQ-8.
* **Is not**: selective, calibrated, or safety-grade. One output equals one
  mixture of CO, H2, ethanol, ammonia and methane; the community thread reports
  widely differing offsets, occasionally stuck readings, and the vendor itself
  says "calibration is required, do not use it for safety".

## Wiring (5 V analog module)

```
module 5V  -> 5 V (the devkit's 5V pin, ~90 mA during the heater cycle)
module GND -> GND (same node as the ESP32)
module A0  -> divider -> ADC1 pin          (or directly into an ADS1115 input)
module EN  -> GPIO (+ inverted: true)      (LOW = enabled on the Fermion and clones)
```

* The output swings up to 5 V while the ESP32 ADC stops at ≈ 3.1 V with
  `attenuation: 12db`, so use a divider (e.g. 10 kOhm series / 20 kOhm to ground,
  ratio 2/3) and set `voltage_multiplier: 1.5`. The vendor model only needs the
  ratio to be *compensated*, not to be a specific value.
* Use an ADC1 pin (GPIO32-39); ADC2 is unavailable while Wi-Fi is active.
* **ADS1115 alternative** (recommended if you want resolution): the 16-bit
  ADS1115 with gain `6.144 V` accepts the 0-5 V output directly (no divider) and
  is a `voltage_sampler` in ESPHome, so it can be used as `voltage:` - see the
  community thread post 13 for a wiring example (`Vout -> A2`, `EN -> GPIO4`).
* `EN` on the Fermion breakout and on most clones must be pulled **low** to enable
  the sensor (DFRobot's own driver writes the pin low to wake it), hence
  `enable_pin: { number: GPIO4, inverted: true }`. If your board has no `EN` pad or
  it is already pulled low, remove `enable_pin:`.
* The vendor model does not need the board's load resistor at all. Only if you use
  `conversion: datasheet` must you know it: measure the resistor between `A0` and
  `GND` (the common boards use 10 kOhm, the value the community thread assumes)
  and set `rl:` accordingly.

## Warm-up, burn-in and calibration

1. **Heater warm-up: 3 minutes** (`warmup_time: 3min`). The vendor example also
   warns: do not touch the probe while it warms up, and keep the sensor in clean
   air during that time.
2. **Burn-in: 24 - 48 h** for a brand-new sensor before you trust the values
   (`warmup_time: 24h` suppresses everything during that window).
3. **Calibration in clean air** (`calibration:`) stores the reference - the
   vendor model's `x_air = VCC - V_AO`, the datasheet model's `R0 = RS`. It runs
   after `max(delay, warmup_time)`, averages `samples` readings (the vendor library
   averages 10) and is kept in flash. Pin it later with `air_reference:`/`r0:` to
   skip the calibration at boot.
4. Re-calibrate after changing the wiring or `voltage_multiplier`, and every few
   months: these sensors drift.

## ESP32 ADC limitations

* Below ≈ 75 mV the ADC of a classic ESP32 cannot measure reliably, which the
  community thread translates to ≈ 7 ppm CO - and to an equivalent floor for H2.
  Fuel the module through a divider that keeps the interesting range above that,
  or use an ADS1115.
* ESP32 ADC readings are noisy: `samples: 4` (default) averages four conversions
  per update, `samples: 16` is worth trying if your values jitter. A 100 nF
  capacitor from the ADC pin to ground also helps.

## Role in this project (H2 battery room)

| Sensor | Band | Role |
|---|---|---|
| MiCS-5524 (`packages/mics5524.yaml`) | 100 - 1000 ppm (0.25 - 2.5 % LEL) | trace / early detection, trend before the MQ-8 reacts |
| MQ-8 (`packages/mq8.yaml`) | 4 000 - 10 000 ppm (10 - 25 % LEL) | pre-alarm at the NFPA 855 design target |

Thresholds and the reasoning behind them: [`h2_thresholds.md`](h2_thresholds.md).
Both packages can be included in the same device (they only share the I2C-free
defaults) - include `mics5524` *in addition to* one MQ-8 package, and keep the
alerting in Home Assistant.

```yaml
packages:
  mq8: !include packages/mq8.yaml
  mics5524: !include packages/mics5524.yaml
```

## Confidence checklist before trusting a reading

* ratio diagnostic ≈ 1.0 in clean air, and it *drops* when a reducing gas arrives;
* the AO voltage moves when you breathe near the sensor (a cheap sanity test);
* the value returns to ~0 ppm after the gas clears;
* no `analog output reads 0.0000 V` warning in the log.

If any of those fail, re-check the wiring, the EN polarity and the calibration
before looking at the model.

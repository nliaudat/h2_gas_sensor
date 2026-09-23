# MiCS-5524 conversion models and their provenance

The component can convert the analog output with two different models. They are
**not** interchangeable: they are fitted against different quantities.

Implemented in
[`../esphome/components/mics_5524_gas_sensor/mics_math.h`](../esphome/components/mics_5524_gas_sensor/mics_math.h)
and asserted by
[`../esphome/tests/mics_math_test.cpp`](../esphome/tests/mics_math_test.cpp).

## 1. `conversion: dfrobot` (default) - the vendor model

```
x       = VCC - V_AO                     VCC = module supply (5 V), V_AO in volts
ratio   = x / x_air                      ~1.0 in clean air, drops with gas
ppm     = (threshold - ratio) / gain     per gas, clamped to the vendor range
```

* `x_air` is captured during the clean-air calibration, so the model needs
  neither the board's load resistor nor its exact divider ratio. That is a real
  advantage for the unlabeled 5 V / GND / A0 / EN modules: only the *ratio*
  matters and it is normalised by the calibration.
* The formula is linear in the ratio (the vendor's own model), *not* the log-log
  curve of the datasheet.

| Gas | threshold | gain | vendor range | reported as 0 below |
|---|---|---|---|---|
| CO | 0.425 | 0.000405 | 1 - 1000 ppm | 1 ppm |
| H2 | 0.279 | 0.00026 | 1 - 1000 ppm | 1 ppm |
| NH3 | 0.8 | 0.0015 | 1 - 500 ppm | 10 ppm |
| C2H5OH | 0.306 | 0.00057 | 10 - 500 ppm | 10 ppm |
| CH4 | 0.786 | 0.000023 | 1000 - 25000 ppm | 1000 ppm |

Source: `DFRobot_MICS` (MIT, `DFRobot/DFRobot_MICS`), `DFRobot_MICS.cpp` -
`getGasData()` and `getCarbonMonoxide()` … `getNitrogenDioxide()`, cross-checked
against the Python port `python/raspberrypi/DFRobot_MICS.py`. The same library
backs the makerguides tutorial
<https://www.makerguides.com/fermion-mems-multi-gas-sensor-mics-5524-with-arduino/>,
which is also where the 3-minute warm-up and the "do not touch the sensor while
warming up, place it in clean air" instruction come from.

### Worked example (checked by the host test)

```
x_air = 5.0 V - 0.5 V = 4.5 V          (clean air, V_AO = 0.5 V)
V_AO  = 4.5 V  ->  x = 0.5 V  ->  ratio = 0.5 / 4.5 = 0.1111
H2: (0.279 - 0.1111) / 0.00026 = 645.7 ppm
V_AO  = 3.2 V  ->  ratio = 0.4  ->  0.4 > 0.279  ->  "not detected" -> 0 ppm
```

Because the vendor clamp is `ppm < min -> 0`, a reading within one gain step of
the minimum rounds to 0 (for H2 that is one step ≈ 1 ppm, for CH4 ≈ 1000 ppm).

## 2. `conversion: datasheet` - the log-log fit

```
RS      = (VCC * RL) / V_AO - RL          RL = board load resistor (kOhm)
ratio   = RS / R0                         R0 = RS in clean air -> ratio 1.0
ppm     = a * ratio^b
```

This is the chain used by the datasheet graph (and by the sibling
`mq_gas_sensors` component). Unlike the MQ-8 there is no "clean air ratio" offset:
`R0` is simply `RS` measured in clean air, so the ratio is 1.0 there and the
curve returns `a`.

### Deriving `a` and `b` from two datasheet points

The datasheet plot is log-log, so two points define the fit:

```
b = log10(ppm2 / ppm1) / log10(ratio2 / ratio1)
a = 10^(log10(ppm1) - b * log10(ratio1))
```

Only CO ships with coefficients, taken from the Home Assistant community thread
*"CO sensor - MICS-5524 or MICS-6814"* (post 8):

| Point | value |
|---|---|
| p1 | 10 ppm at RS/R0 = 0.5 |
| p2 | 1000 ppm at RS/R0 = 0.01 |
| result | `a = 6.3`, `b = -1.1` |

How good is a two-point fit? Checked by the host test: it returns 13.5 ppm at
RS/R0 = 0.5 (the datasheet point is 10 ppm) and 998.5 ppm at 0.01 (datasheet:
1000 ppm) - a few tens of percent in the low decade, which is well inside the
sensor's own spread. For every other gas supply your own `a:`/`b:` derived with
the two-point recipe.

The same thread's later posts (11) propose fits of the form
`A * ratio^B * exp(C * ln ratio)`, which algebraically collapse to a plain power
law `A * ratio^(B + C)` - so nothing extra is gained by that form, and the
component does not use it.

## Not implemented (on purpose)

| Vendor feature | Why not |
|---|---|
| NO2 (`getNitrogenDioxide`) | The vendor code gates it with `ratio >= 1.1` and then computes `(ratio - 0.045) / 6.13`, which can never reach its own 10 ppm ceiling; DFRobot also documents NO2 only for the MiCS-2714/4514 boards. |
| `getGasExist()` (per-gas "detected" booleans) | Thresholds are the same ratios as above, so a Home Assistant threshold on the published ppm is equivalent - and alerting belongs in Home Assistant (see `.ai/instructions.md` §7.3). |
| Duty-cycling the heater through the EN pin | The vendor warm-up *is* the calibration; sleeping would invalidate the clean-air reference. |

## Accuracy

* One output, five gases: the reading is a mixture, the sensor cannot say which
  gas is present (quoted as `SELECTIVITY_NOTE` from `coefficients.py`).
* Sensor-to-sensor spread is large; the community thread reports users seeing
  stuck or implausible CO/NO2 values, hence the "do not treat it as a safety
  device" note in the platform and in `docs/mics5524_guide.md`.
* The ESP32 ADC cannot read below ~75 mV, which is ≈ 7 ppm CO with the thread's
  divider - use an ADS1115 if the low decade matters.

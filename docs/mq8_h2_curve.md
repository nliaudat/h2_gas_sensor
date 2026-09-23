# The MQ-8 H2 curve and how a ppm value is produced

Everything in this document is implemented in
[`../esphome/components/mq_gas_sensors/mq_math.h`](../esphome/components/mq_gas_sensors/mq_math.h)
and asserted by [`../esphome/tests/mq_math_test.cpp`](../esphome/tests/mq_math_test.cpp).

## Measurement chain

```
V       = adc × volt_resolution / (2^bits − 1)   the ADC platform does this
V_ao    = V × voltage_multiplier                 restore the divider (+1.5 for 2/3)
RS      = (VCC × RL) / V_ao − RL                 sensor resistance, kOhm
ratio   = RS / R0                                R0 = the value from the calibration
PPM     = a × ratio^b     with a = 976.97, b = −0.688
```

`R0` comes from the calibration in clean air:

```
R0 = RS_air / ratio_in_clean_air       ratio_in_clean_air = 70 for the MQ-8
```

RS/R0 is therefore **70 in clean air**, and 52.5 ppm is what the curve returns
there — the datasheet curve only starts at 100 ppm, so the clean-air value is
already an extrapolation and "≈ 50 ppm in clean air" is the expected baseline of
this sensor family.

## Why `ratio_in_clean_air` is 70

`70` is the `RatioMQ8CleanAir` value used by every published MQ-8 example
(`MQSensorsLib`, SolderedElectronics) together with the `a`/`b` pair above: the
pair was fitted against `RS/R0` with that reference, which is why the component
defaults to `ratio_mode: rs_r0`.

`MQUnifiedsensor::readSensorR0Rs()` computes the ratio the other way round
(`R0/RS`, see the source comment *"INVERTED for MQ-131 issue 28"*). With that
convention and the published coefficients, a clean-air MQ-8 would report
≈ 18 000 ppm instead of ≈ 50 ppm. Use `ratio_mode: r0_rs` only with coefficients
you fitted against `R0/RS` yourself.

## Ratio → ppm (a = 976.97, b = −0.688, R0 = 0.2899 kOhm)

| RS/R0 | RS (kΩ) | ppm |
|---|---|---|
| 70 (= clean air) | 20.29 | 52.5 |
| 35 | 10.15 | 84.6 |
| 17.5 | 5.07 | 136.4 |
| 8.75 | 2.54 | 219.7 |
| 4.375 | 1.27 | 353.9 |
| 2.1875 | 0.63 | 570.2 |
| 1.09375 | 0.32 | 918.6 |
| 1.0 | 0.29 | 977.0 |

Rules of thumb: halving the ratio multiplies ppm by `2^0.688 = 1.611`; a factor
of 10 in ppm corresponds to a factor of `10^(1/0.688) = 28.4` in the ratio.

The curve is an inverse power law: as RS drops (more hydrogen), the ratio drops
and the ppm value rises. Note that the published pairs are **not** unique — see
[`mqdatascience_comparison.md`](mqdatascience_comparison.md) for a second dataset
that differs by a constant ≈ 11 % (same slope).

## Provenance of `a = 976.97`, `b = −0.688`

* carried by `sensorConfigData.h` of the SolderedElectronics MQ library, which
  itself carries the `MQUnifiedsensor` curves;
* cross-checked against the per-sensor tables of the `miguel5612/MQSensorsLib`
  examples (MQ-4 CH4 `1012.7 / −2.786`, MQ-3 alcohol `0.3934 / −1.504`, MQ-8 H2
  `976.97 / −0.688`, …);
* exposed by MQDataScience as `a = 18391.5667, b = −1.4494` in its inverse form,
  which is the same curve anchored differently (see the comparison document).

The numbers are a **fit of the datasheet log-log plot** (100 – 10 000 ppm H₂), so
they inherit its uncertainty: expect ±30 % sensor-to-sensor spread, a drift over
weeks and a cross-sensitivity to alcohol, LPG, methane and CO (see the
`gases:` map in `coefficients.py` for the alternative curves of those gases).

## Guards

* AO ≤ 10 mV (open circuit, missing 5 V supply, wrong divider) → the reading is
  logged as a warning and published as `unknown`, never as `0 ppm`.
* `log10(PPM)` outside the float range → clamped to `max_ppm` (`10000` by
  default) instead of overflowing.
* Nothing is published during the warm-up window, while a calibration is pending
  or running, and while `R0` is unknown.

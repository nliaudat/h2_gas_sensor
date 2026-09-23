# MQDataScience comparison — what was adopted and what was skipped

[abcdaaaaaaaaa/MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience)
(library name `MQSpaceData`, v6.0.0, MIT, 21 MQ sensor families) is the reference
for the two optional extras of this component: the **temperature/humidity
compensation** and an **alternative MQ-8 H₂ coefficient dataset**. This document
records the differences and the decisions, so that neither the numbers nor the
omissions are a surprise later.

## MQ-8 H₂ curve: two fits of the same datasheet curve

| Dataset | Form | Clean air (RS/R₀ = 70) |
|---|---|---|
| SolderedElectronics / MQUnifiedsensor (this component's default, `curve: standard`) | `ppm = 976.97 × ratio^(−0.688)` | 52.5 ppm |
| MQDataScience (`curve: mqdatascience`) | `ppm = (ratio / 18391.5667)^(1 / −1.4494)` | 46.7 ppm |

MQDataScience's inverse form is the same power law re-parameterised:
`a′ = a^(−1/b) = 875.4`, `b′ = 1/b = −0.68994`, i.e. `ppm = 875.4 × ratio^(−0.68994)`.

| RS/R₀ | standard | mqdatascience | Δ |
|---|---|---|---|
| 70 (clean air) | 52.5 | 46.7 | −11.1 % |
| 20 | 124.4 | 110.8 | −10.9 % |
| 10 | 200.4 | 178.8 | −10.8 % |
| 5 | 322.8 | 288.4 | −10.7 % |
| 3 | 458.8 | 410.2 | −10.6 % |
| 1.6 | 707.0 | 633.0 | −10.5 % |
| 1.0 | 977.0 | 875.4 | −10.4 % |

**Conclusion:** the slope is identical (0.688 vs 0.68994, 0.3 % apart), the two
curves differ by a constant factor of **0.896** — MQDataScience anchors clean air
at ≈ 47 ppm instead of ≈ 52 ppm. Their dataset is therefore **not more
accurate**, it is the same datasheet fit with a different clean-air anchor. Since
switching it would silently shift every statistic in Home Assistant (about
−1 100 ppm at the 10 000 ppm pre-alarm), the default stays `standard` and the
variant is only for A/B comparison:

```yaml
  - platform: mq_gas_sensors
    sensor_type: MQ-8
    gas: H2
    curve: mqdatascience       # optional, default: standard
    # ... no a:/b: here - the dataset defines them itself
```

Both datasets are verified by the host test (including the −11 % offset), and
`esphome config` rejects `curve:` combined with explicit `a:`/`b:`.

## Temperature/humidity compensation — adopted

Ported one-to-one from their `src/Correction.cpp` (see
[`temperature_humidity_correction.md`](temperature_humidity_correction.md)):
`correction = a + c × exp(b × T)` with `a`, `b`, `c` interpolated linearly between
RH 33 % and RH 85 %, `T` clamped to −10 … 50 °C. Applied as
`ratio_eff = ratio / correction`, which is algebraically identical to their
`(ratio / (a × correction))^(1/b)`.

Deviations, both deliberate:

| Their behaviour | Here | Why |
|---|---|---|
| `limit(value, 0, maxPpm × correction)` | `correction_clamp: absolute` (default) keeps `max_ppm` | a safety ceiling must not drop to ≈ 8 900 ppm in warm humid air; `scaled` reproduces their behaviour |
| `calculateCorrection()` supports MQ-9/MQ-131 with a second, RH 30/60/85 % segment | not ported | only the one-segment models are listed; MQ-9/MQ-131 with the correction are rejected at compile time instead of silently using the wrong constants |

## Skipped on purpose

| MQDataScience feature | Reason |
|---|---|
| `calculateCalValue1/2()` calibration (the `air`, `calibrateAir`, `rlcal`, `calValue` parameterisation) | it is a **divider-based, R0-free** calibration: their `sensorVal` is the raw ADC fraction of full-scale, so the model absorbs the module's load resistor and the reference voltage. This component keeps `RS = (VCC × RL)/V − RL` with an explicit `RL`, an optional `voltage_multiplier` for the divider and a flash-persisted `R0` — the same physics, but the hardware values stay visible and configurable. Their `ratio` variable reduces to `air × RS/RS_air` for a given `calValue`, i.e. to the same RS/R₀ used here (with `air = 70`, `rlcal = 1` for the MQ-8), so nothing is lost. |
| STEL limits, preheat times, RL values, voltage ranges | published as **PNG images** in their README, not as machine-readable tables; the H₂-relevant values are in [`h2_thresholds.md`](h2_thresholds.md) and [`mq8_sensor_guide.md`](mq8_sensor_guide.md) |
| `AirQuality.cpp` | only an exponential interpolation between `ppm_min`/`ppm_max`, no threshold table |
| the Python platform (`DataScience/`: 76 regression/ML models, Cross-Validation, R², 3D ppm surfaces, 4D curve prediction) | offline analysis tooling that needs their Python environment; it cannot run on an ESP32 and does not change the on-device math |
| their extra types (MQ-216, MQ-303B, MQ-306A, MQ-307A) | outside the scope of this project (MQ-8 only); their MQ-8 `air` value is still used for the cross-check above |

## Sources and attribution

* MQDataScience — `abcdaaaaaaaaa`, MIT, `library.properties` reports
  `name=MQSpaceData, version=6.0.0`; used: `src/SensorDefinitions.cpp` (MQ-8
  curve), `src/Correction.cpp` (T/RH model), `src/GasSensor.cpp` (formulas).
* `src/SensorDefinitions.cpp` also carries the per-gas metadata of the MQ-8:
  `air = 70.0`, `calibrateAir = 1.0`, `rlcal = 1.0`, `useCorrection = true`,
  H₂ domain 200 – 10 000 ppm.
* The default curve (`976.97 / −0.688`) comes from SolderedElectronics /
  MQUnifiedsensor, see [`mq8_h2_curve.md`](mq8_h2_curve.md).
* Unavailable for verification: the `zbotic.in` MQ-8 article (HTTP 403 +
  Cloudflare CAPTCHA) and the image-only tables mentioned above.

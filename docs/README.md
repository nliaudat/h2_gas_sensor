# H2 gas sensor — documentation

Hydrogen monitoring for a battery room (nickel-iron / Edison cells) with an
ESP32, an **MQ-8** sensor and ESPHome. This folder is the versioned
documentation; the measured numbers are asserted by
[`../esphome/tests/mq_math_test.cpp`](../esphome/tests/mq_math_test.cpp), so the
docs and the firmware cannot drift apart silently.

`documentation/` (local only, **git-ignored**) holds the private reading notes,
the article list and the reference PDF.

## Documents

| Document | Content |
|---|---|
| [`../.ai/instructions.md`](../.ai/instructions.md) | Rule book for contributors and AI agents: lint commands, C++/Python/YAML style, domain and safety rules |
| [`mq8_sensor_guide.md`](mq8_sensor_guide.md) | Wiring, 5 V supply, AO divider, real load resistor, burn-in, R0 calibration workflow, placement |
| [`mq8_h2_curve.md`](mq8_h2_curve.md) | The measurement chain V → RS → RS/R0 → ppm, why `ratio_in_clean_air` is 70, provenance of `a`/`b`, ratio→ppm table |
| [`h2_thresholds.md`](h2_thresholds.md) | LEL/ppm conversion, the 4 000 / 10 000 / 20 000 ppm thresholds, where to alert |
| [`temperature_humidity_correction.md`](temperature_humidity_correction.md) | The optional MQDataScience T/RH compensation: model, constants, effect envelope, configuration, safety rules |
| [`mqdatascience_comparison.md`](mqdatascience_comparison.md) | MQ-8 H2 curve comparison (standard vs MQDataScience), what was adopted and what was deliberately skipped |
| [`mics5524_guide.md`](mics5524_guide.md) | MiCS-5524 hardware, wiring (divider / ADS1115 / EN pin), warm-up and calibration, ADC limits, role next to the MQ-8 |
| [`mics5524_conversion.md`](mics5524_conversion.md) | The two MiCS-5524 conversion models (vendor vs datasheet), the constants table, the 2-point fit recipe, what is not implemented |

## Firmware layout

```
esphome/
├── config.yaml                     entry point (substitutions + package includes)
├── packages/
│   ├── mq8.yaml                    MQ-8 only (default, no compensation)
│   ├── mq8_tc.yaml                 MQ-8 + I2C T/RH sensor + MQDataScience compensation
│   ├── mics5524.yaml               MiCS-5524 trace sensor (optional hardware)
│   ├── board.yaml, wifi.yaml, time.yaml, sensors_others.yaml, switch.yaml
├── components/mq_gas_sensors/      MQ-2 ... MQ-309A component (see its README.md)
├── components/mics_5524_gas_sensor/ MiCS-5524 component (see its README.md)
└── tests/                          host tests + config validation fixtures
```

Only one MQ-8 package may be included at a time — both define `id: mq8`. The
`mics5524` package is additive (it uses `id: mics`).

Build, flash and test:

```bash
cd esphome
esphome config config.yaml            # validate the configuration
esphome compile config.yaml           # full ESP-IDF build
esphome run config.yaml               # flash (OTA or serial)

cd tests
g++ -std=c++17 -O2 -I ../components/mq_gas_sensors mq_math_test.cpp -o mq_math_test && ./mq_math_test
g++ -std=c++17 -O2 -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test && ./mics_math_test
```

## Verification status

**Verified** (asserted by the host test):

* the measurement chain (`voltage_from_adc`, `rs_from_voltage`, `ratio_from_rs`,
  `r0_from_clean_air`, `ppm_from_ratio`), including the overflow/NaN guards;
* the MQ-8 H2 curve values in these documents (`a = 976.97`, `b = -0.688`,
  RS/R0 = 70 in clean air → 52.5 ppm);
* the MQDataScience alternative curve (`a = 18391.5667`, `b = -1.4494`, inverse
  form) and its −11 % offset against the standard dataset;
* every correction factor quoted in
  [`temperature_humidity_correction.md`](temperature_humidity_correction.md)
  (e.g. RH 40 %/20 °C → 0.8997, i.e. ppm × 0.9297).

**Verified** (ESPHome 2026.9.0 / ESP-IDF 5.5.5, 23.09.2026):

* `esphome config config.yaml`, `esphome config tests/test_no_id.yaml` and
  `esphome config tests/test_tc.yaml` all report `Configuration is valid!`;
* `esphome compile config.yaml` succeeds with `packages/mq8.yaml` **and** with
  `packages/mq8_tc.yaml` (`Successfully compiled program.`, no compiler
  warnings, 46.1 % flash with the plain package);
* the compile-time rejections behave as documented: `correction_mode` without
  `temperature:`/`humidity:`, `curve:` combined with `a:`/`b:`, and
  `correction_mode` on a type without constants (MQ-9) each fail with an
  explanatory message and the list of supported types;
* `flake8 --config .flake8 components/mq_gas_sensors tests` is clean; the host
  test builds warning-free with `-Wall -Wextra`.

**Verified** (ESPHome CI compliance, 23.09.2026 - see
[`../.ai/instructions.md`](../.ai/instructions.md) section 3):

* `python script/ci-custom.py` (ESPHome's own CI linter, vendored) reports
  **0 findings** - it reported 2 492 before the cleanup (line endings, trailing
  whitespace, non-ASCII characters, namespace);
* `clang-format --dry-run --Werror` is clean with the **pinned v13.0.1** (newer
  clang-format versions format differently against this `.clang-format`);
* `yamllint -c .yamllint .`, `flake8 --config .flake8 components tests script`,
  `ruff check .` and `ruff format --check .` are all clean;
* the component lives in `esphome/components/mq_gas_sensors` (lower case) and the
  YAML platform is `platform: mq_gas_sensors`, as ESPHome's namespace check
  requires.

**Verified** (MiCS-5524 support, 23.09.2026 - same rule book as above):

* every constant and formula in [`mics5524_conversion.md`](mics5524_conversion.md)
  is asserted by
  [`../esphome/tests/mics_math_test.cpp`](../esphome/tests/mics_math_test.cpp):
  the vendor thresholds/gains, the per-gas minimum clamps, the `x / x_air`
  normalisation, the CO two-point fit (13.5 ppm at ratio 0.5, 998.5 ppm at 0.01)
  and the NaN/overflow guards;
* `esphome config` is valid for `tests/test_mics.yaml` (both models, shared
  voltage source, diagnostics, pinned reference, alias `gas: methane`) and
  `tests/test_mics_package.yaml` (the shipped package), while
  `conversion: dfrobot` combined with `a:`/`b:` or `rl:`,
  `conversion: datasheet` combined with `air_reference:`, and a datasheet gas
  without coefficients are each rejected with an explanatory message;
* `esphome compile config.yaml` succeeds with `packages/mq8.yaml` **and**
  `packages/mics5524.yaml` included at the same time (`Successfully compiled
  program.`, no compiler warnings, 46.4 % flash - +7 KB for the new component);
* `python script/ci-custom.py` reports 0, clang-format **v13.0.1** is clean, and
  `yamllint` / `flake8` / `ruff check` / `ruff format --check` are clean for the
  new component.

**Verified** (divider guard, 23.09.2026 - see
[`../.ai/instructions.md`](../.ai/instructions.md) section 3):

* `esphome config` **rejects** a 5 V module wired straight to an ADC pin
  (`voltage_multiplier: 1.0` with `pin:`) in both components, naming the computed
  voltage and suggesting `divider: {r1: 10.0, r2: 20.0}` or an `adc_pin_max` for a
  wider sampler;
* the recommended 10k/20k divider (`divider: {r1: 10.0, r2: 20.0}` -> 1.5, i.e.
  3.33 V at the pin) is accepted - silently when `adc_input_max: 3.33` is declared
  (all three packages do) and with the informational "top of the range may clip"
  warning otherwise (`tests/test_no_id.yaml` keeps that path covered);
* an ADS1115-style configuration (no divider, `voltage_multiplier: 1.0`,
  `adc_input_max`/`adc_pin_max: 6.144`) is accepted;
* both host tests pass and `esphome config` is valid for all five fixtures.

**Unverified / unavailable:**

* the article `zbotic.in/mq-8-hydrogen-sensor-detect-h2-gas-for-battery-monitoring/`
  is behind a Cloudflare CAPTCHA (HTTP 403) — its numbers could not be
  cross-checked;
* the STEL / preheat / RL tables of MQDataScience are **PNG images** in their
  README, not machine-readable tables, so only the MQ-8 relevant values are
  quoted here;
* their "76 models / 3D surface / 4D prediction" platform was **not** reproduced
  (it needs their Python environment and does not run on an ESP32);
* no measurement was taken against a calibrated hydrogen source: the curve is a
  datasheet fit and the sensor-to-sensor spread of an MQ-8 is around ±30 %.

## Sources

| Source | Used for |
|---|---|
| [SolderedElectronics/Soldered-MQ-Gas-Sensor-Arduino-Library](https://github.com/SolderedElectronics/Soldered-MQ-Gas-Sensor-Arduino-Library) | `sensorConfigData.h` curve table of the component (`coefficients.py`) |
| [miguel5612/MQSensorsLib](https://github.com/miguel5612/MQSensorsLib) | original MQUnifiedsensor PPM model (`mq_math.h`) |
| [RapportTecnologia esp-iot-solution MQSensorLIB](https://github.com/RapportTecnologia/esp-iot-solution/tree/MQSensorLib/components/sensors/gas/MQSensorLIB) | ESP-IDF port + ratio discussion |
| [abcdaaaaaaaaa/MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience) (MIT, v6.0.0 "MQSpaceData") | T/RH correction model, alternative MQ-8 H2 curve, reference tables |
| `documentation/H2_lie_limits.txt`, `documentation/9.2024SmartAirMonitoring…pdf` | H2 LEL reference values (local, git-ignored) |

The MQ-8 datasheet and the MQUnifiedsensor/MQDataScience fits describe the
**same** log-log curve; the differences between the datasets are documented in
[`mqdatascience_comparison.md`](mqdatascience_comparison.md).

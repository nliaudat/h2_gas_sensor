# H2 gas sensor - documentation

Hydrogen monitoring for a battery room (nickel-iron / Edison cells) with an
ESP32, an **MQ-8** sensor and ESPHome. This folder is the versioned, user-facing
documentation; the numbers quoted here are asserted by the host tests
([`../esphome/tests/mq_math_test.cpp`](../esphome/tests/mq_math_test.cpp),
[`../esphome/tests/mics_math_test.cpp`](../esphome/tests/mics_math_test.cpp)), so
the docs and the firmware cannot drift apart silently.

## Documents

| Document | Content |
|---|---|
| [`../README.md`](../README.md) | Entry point: what the project is, bill of materials, wiring diagram, quick start |
| [`getting_started.md`](getting_started.md) | Setup: secrets, substitutions, packages, wiring, first flash, burn-in, calibration |
| [`home_assistant_alerts.md`](home_assistant_alerts.md) | Entities, thresholds, alert automations, day-to-day operations |
| [`troubleshooting.md`](troubleshooting.md) | Setup-time and runtime symptoms, and how to read the log line |
| [`development.md`](development.md) | Validate, build, test and lint commands for contributors |
| [`../esphome/readme.md`](../esphome/readme.md) | Firmware reference: packages, wiring, calibration, operations, credits, licence |
| [`../.ai/instructions.md`](../.ai/instructions.md) | Rule book: lint commands, C++/Python/YAML style, domain and safety rules |
| [`mq8_sensor_guide.md`](mq8_sensor_guide.md) | MQ-8 wiring, 5 V supply, AO divider, real load resistor, burn-in, R0 calibration workflow, placement |
| [`mq8_h2_curve.md`](mq8_h2_curve.md) | The measurement chain V → RS → RS/R0 → ppm, why `ratio_in_clean_air` is 70, provenance of `a`/`b`, ratio→ppm table |
| [`h2_thresholds.md`](h2_thresholds.md) | LEL/ppm conversion, the 4 000 / 10 000 / 20 000 ppm thresholds, where to alert |
| [`temperature_humidity_correction.md`](temperature_humidity_correction.md) | The optional MQDataScience T/RH compensation (packages `mq8_sht4x.yaml` / `mq8_dht11.yaml`): model, constants, effect envelope, configuration, safety rules |
| [`mqdatascience_comparison.md`](mqdatascience_comparison.md) | MQ-8 H2 curve comparison (standard vs MQDataScience), what was adopted and what was deliberately skipped |
| [`mics5524_guide.md`](mics5524_guide.md) | MiCS-5524 hardware, wiring (divider / ADS1115 / EN pin), warm-up and calibration, ADC limits, role next to the MQ-8 |
| [`mics5524_conversion.md`](mics5524_conversion.md) | The two MiCS-5524 conversion models (vendor vs datasheet), the constants table, the 2-point fit recipe, what is not implemented |

## Sources

| Source | Used for |
|---|---|
| [SolderedElectronics/Soldered-MQ-Gas-Sensor-Arduino-Library](https://github.com/SolderedElectronics/Soldered-MQ-Gas-Sensor-Arduino-Library) | `sensorConfigData.h` curve table of the component (`coefficients.py`) |
| [miguel5612/MQSensorsLib](https://github.com/miguel5612/MQSensorsLib) | original MQUnifiedsensor PPM model (`mq_math.h`) |
| [RapportTecnologia esp-iot-solution MQSensorLIB](https://github.com/RapportTecnologia/esp-iot-solution/tree/MQSensorLib/components/sensors/gas/MQSensorLIB) | ESP-IDF port + ratio discussion |
| [abcdaaaaaaaaa/MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience) (MIT, v6.0.0 "MQSpaceData") | T/RH correction model, alternative MQ-8 H2 curve, reference tables |
| NFPA 855 (stationary energy storage installation) and the physical LEL of hydrogen (4 % vol = 40 000 ppm) | the threshold tables in [`h2_thresholds.md`](h2_thresholds.md) |

The MQ-8 datasheet and the MQUnifiedsensor/MQDataScience fits describe the
**same** log-log curve; the differences between the datasets are documented in
[`mqdatascience_comparison.md`](mqdatascience_comparison.md).

## Licence

This project is dual-licensed under the **Apache License 2.0** *or* the **MIT
License**, at your option ([`../LICENSE`](../LICENSE),
[`../LICENSE-APACHE`](../LICENSE-APACHE),
[`../LICENSE-MIT`](../LICENSE-MIT)). The third-party material used by the firmware
(MQUnifiedsensor, SolderedElectronics, MQDataScience, DFRobot_MICS, the vendored
ESPHome CI script) is listed with its own licence in
[`../esphome/readme.md#licence`](../esphome/readme.md#licence).

# H2 sensor board - ESPHome firmware

ESPHome firmware for a battery-room **hydrogen monitor**: an ESP32 reads an MQ-8
(and optionally a MiCS-5524) gas sensor, converts the analog output into a ppm
value and publishes it to Home Assistant.

| | |
|---|---|
| Board | ESP32 devkit (`az-delivery-devkit-v4` by default), ESP-IDF framework |
| Sensors | MQ-8 (4000 - 10000 ppm band, pre-alarm) + optional MiCS-5524 (100 - 1000 ppm trace band) |
| Optional | SHT4x (I2C) for the MQDataScience temperature/humidity compensation |
| Components | `components/mq_gas_sensors/` (MQ-2 ... MQ-309A) and `components/mics_5524_gas_sensor/` (MiCS-5524) |
| Documentation | [`../docs/`](../docs/README.md) (hardware, curves, thresholds, conversions) and [`../.ai/instructions.md`](../.ai/instructions.md) (the rule book) |

The measurement chain, the safety rules and the pitfalls live in the docs - read
[`../docs/mq8_sensor_guide.md`](../docs/mq8_sensor_guide.md) (or
[`../docs/mics5524_guide.md`](../docs/mics5524_guide.md)) before wiring anything.

---

## Before you start - user-editable files

| File | Purpose | Usually edited? |
|---|---|---|
| `secrets.yaml` | WiFi credentials + fallback AP password (**git-ignored**, create it - see `packages/wifi.yaml` for the keys) | **Always** |
| `config.yaml` | `substitutions:` (`name`, `friendly_name`, `board_type`, `TZ`) and the package includes | **Always** |
| `packages/mq8.yaml` | MQ-8 pins/divider (`mq8_pin`, `mq8_divider_r1/r2`, `mq8_rl`) | Yes |
| `packages/mq8_tc.yaml` | Same, plus the I2C T/RH sensor and the MQDataScience correction | Only with an SHT4x |
| `packages/mics5524.yaml` | MiCS-5524 pins/divider/EN (optional hardware) | Only with a MiCS-5524 |
| `packages/wifi.yaml` | WiFi networks (`!secret` references) | Almost always |
| `packages/board.yaml` | ESP-IDF, watchdog/sdkconfig, API/OTA, safe mode | Rarely |
| `packages/time.yaml` | SNTP + the weekly 06:00 restart | Rarely |
| `packages/switch.yaml`, `packages/sensors_others.yaml` | Restart switch, WiFi signal diagnostics | Rarely |

## Repository layout

```
esphome/
├── config.yaml                       entry point: substitutions + package includes
├── secrets.yaml                      credentials (git-ignored)
├── components/mq_gas_sensors/        MQ sensor platform (see its README.md)
├── components/mics_5524_gas_sensor/  MiCS-5524 platform (see its README.md)
├── packages/                         mq8.yaml, mq8_tc.yaml, mics5524.yaml, board.yaml, wifi.yaml, ...
├── script/                           vendored ESPHome CI linter + wrapper (see its README.md)
├── tests/                            host tests + config validation fixtures
└── .clang-format .clang-tidy .flake8 .yamllint .pre-commit-config.yaml pyproject.toml
```

## Packages - pick the right combination

```yaml
packages:
  wifi: !include packages/wifi.yaml
  board: !include packages/board.yaml
  time: !include packages/time.yaml
  sensors_others: !include packages/sensors_others.yaml
  switch: !include packages/switch.yaml
  mq8: !include packages/mq8.yaml          # or mq8_tc.yaml - never both (both use id: mq8)
  # mics5524: !include packages/mics5524.yaml   # additive, optional hardware
```

| Package | Sensor | Notes |
|---|---|---|
| `mq8.yaml` | MQ-8 | plain, no compensation (default) |
| `mq8_tc.yaml` | MQ-8 + SHT4x | adds the MQDataScience temperature/humidity correction |
| `mics5524.yaml` | MiCS-5524 | additive (`id: mics`), trace band, own calibration |

Only **one** MQ-8 package may be included: both define `id: mq8`. `mics5524.yaml`
can be added on top of either and keeps the defaults of the MQ-8 package
untouched.

## Wiring in one screen

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

* The analog output can reach 5 V while the **ESP32 ADC pins are not 5 V
  tolerant** (absolute maximum VDD + 0.3 V = 3.6 V): always keep the divider.
  Never wire the sensor output straight to a GPIO.
* The recommended 10k/20k divider maps 5 V to 3.33 V, a hair above the ADC's
  3.3 V recommended maximum - the packages therefore declare
  `adc_input_max: 3.33`, and the component **rejects** any configuration whose
  divider could exceed `adc_pin_max` (3.6 V) at compile time. Use 10k/10k
  (`r1: 10.0, r2: 10.0`, 5 V -> 2.5 V) or an ADS1115 (0-5 V input,
  `voltage_multiplier: 1.0`, `adc_input_max`/`adc_pin_max: 6.144`) instead.
* Use an ADC1 pin (GPIO32-39); ADC2 is unusable while WiFi is active, and GPIO12
  must not be used.

## First run

1. Copy `secrets.yaml.example`-style credentials into `secrets.yaml` (create the
   file - it is git-ignored) and adjust the `substitutions:` in `config.yaml`
   (`name`, `friendly_name`, `board_type`, `TZ`).
2. Validate and flash:

   ```bash
   esphome config config.yaml        # fast validation
   esphome compile config.yaml       # full ESP-IDF build
   esphome run config.yaml           # flash over USB or OTA
   ```

   For the first **serial** flash: hold `BOOT` for 2-3 s while the connection
   initialises. After an OTA update press `EN` once to run the new firmware.
3. Let the sensor burn in (24 - 48 h for a new MQ-8; the packages use
   `warmup_time: 0s`, set `24h` while it stabilises).
4. Calibrate in **clean air**: `r0` for the MQ-8 (`calibration:` block in
   `mq8.yaml`), the vendor "air reference" for the MiCS-5524. Both are stored in
   flash and logged; they can be pinned in the YAML afterwards
   (`r0:` / `air_reference:`). Re-calibrate after changing the wiring or the
   divider, and every few months - the sensors drift.

   ```yaml
   esphome:
     on_boot:
       - delay: 5min
       - lambda: id(mq8).request_calibration();   # or id(mics).request_calibration()
   ```

## Thresholds and alerting

| H2 concentration | Meaning |
|---|---|
| 100 - 300 ppm | trace / early detection (MiCS-5524 band) |
| 4 000 ppm | 10 % of the LEL - early warning (MQ-8) |
| 10 000 ppm | 25 % of the LEL - pre-alarm, NFPA 855 design target (MQ-8 upper range) |
| 20 000 ppm | 50 % of the LEL - immediate action |

Details and the reasoning: [`../docs/h2_thresholds.md`](../docs/h2_thresholds.md).
Alerting belongs in Home Assistant (or an `on_value` automation), not in the
component - and keep the thresholds on the **absolute** ppm value
(`correction_clamp: absolute`, the default).

## Build, validate, test

```bash
cd esphome
esphome config config.yaml                     # validate the project configuration
esphome compile config.yaml                    # full ESP-IDF build
esphome run config.yaml                        # flash (OTA or serial)

# config-only fixtures (no hardware needed)
esphome config tests/test_no_id.yaml           # MQ-8 without id:, pin: sugar, fixed r0
esphome config tests/test_tc.yaml              # T/RH correction + curve: mqdatascience
esphome config tests/test_mics.yaml            # MiCS: both conversion models, divider, ADS1115 case
esphome config tests/test_mics_package.yaml    # the shipped MiCS package
python tests/inspect_config.py tests/test_no_id.yaml   # show the resolved id

# host tests for the pure math of both components
cd tests
g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mq_gas_sensors mq_math_test.cpp -o mq_math_test.exe && mq_math_test.exe
g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test.exe && mics_math_test.exe
```

## Linting (must be clean before a change is done)

The full rule book is [`../.ai/instructions.md`](../.ai/instructions.md) section 3.

```bash
cd esphome
python script/ci-custom.py                    # ESPHome's own CI checks -> 0 findings
yamllint -c .yamllint .                       # YAML style
flake8 --config .flake8 components tests script
ruff check . && ruff format --check .         # settings in pyproject.toml

# from the git root (the pin is clang-format v13.0.1 - newer versions format differently)
pre-commit run -c esphome/.pre-commit-config.yaml clang-format --all-files
```

`script/ci-custom.py` and `script/helpers.py` are vendored verbatim from
`esphome/esphome` (MIT, see `script/README.md`) and must be run with `esphome/`
as the working directory - the pre-commit hook does that through
`script/run_ci_custom.py`. The host test binaries (`tests/*.exe`) and the
`.esphome/` build cache are git-ignored; never commit them.

## Operations

* **Logging** - `logger:` in `config.yaml` runs at `DEBUG` with per-tag
  overrides; `esphome logs config.yaml` (or the web log) shows the raw values
  (`V=... RS=... ratio=... -> ... ppm`, the applied T/RH correction, the
  calibration result).
* **Weekly restart** - `packages/time.yaml` restarts the board every Monday at
  06:00 (SNTP must be synced first). Useful to recover from long-run drift; the
  calibration is in flash and survives it.
* **Watchdog / performance** - `packages/board.yaml` sets a 30 s task watchdog,
  240 MHz, `FREERTOS_HZ 1000` and TLS 1.3. `preferences.flash_write_interval:
  60min` keeps flash writes (the calibration) gentle; `safe_mode:` and
  `api: reboot_timeout: 30min` provide the usual recovery paths.
* **Dropbox** - the project lives inside a Dropbox folder, and the ESP-IDF build
  cache (`esphome/.esphome/`) gets locked while Dropbox indexes it, which makes
  `esphome compile` fail with `PermissionError: ... being used by another
  process` during ninja's cleanup. Exclude `.esphome/` from the Dropbox sync if
  that happens (or copy the project outside Dropbox to build).

## Troubleshooting (quick pointers)

| Symptom | Where to look |
|---|---|
| state stays `unknown`, `R0 unknown` / `no air reference` | the calibration section above, then the component README |
| `analog output reads 0.0000 V` warning | AO wiring, 5 V supply, EN polarity (MiCS) |
| ppm far too low / flat, or pinned at `max_ppm` | `rl:` (MQ-8), `divider:`/`voltage_multiplier`, `ratio_mode`, curve dataset |
| values jump around | ADC noise - raise `samples`, add 100 nF at the pin, or use an ADS1115 |
| `... can damage the pin` at config time | the divider is too small for a 5 V output: see "Wiring in one screen" |
| values drift over weeks | re-calibrate in clean air (MQ sensors drift) |

Per-sensor details: [`../docs/mq8_sensor_guide.md`](../docs/mq8_sensor_guide.md)
and [`../docs/mics5524_guide.md`](../docs/mics5524_guide.md).

## Documentation index

| Document | Content |
|---|---|
| [`../docs/README.md`](../docs/README.md) | Project documentation index + verification status (what has been verified and how) |
| [`../docs/mq8_sensor_guide.md`](../docs/mq8_sensor_guide.md) | MQ-8 wiring, divider, RL, burn-in, R0 calibration, placement |
| [`../docs/mq8_h2_curve.md`](../docs/mq8_h2_curve.md) | The V -> RS -> RS/R0 -> ppm chain, curve provenance, ratio -> ppm table |
| [`../docs/mics5524_guide.md`](../docs/mics5524_guide.md) | MiCS-5524 hardware, EN pin, ADS1115 option, ADC limits, calibration |
| [`../docs/mics5524_conversion.md`](../docs/mics5524_conversion.md) | Both MiCS conversion models, constants, the 2-point fit recipe |
| [`../docs/h2_thresholds.md`](../docs/h2_thresholds.md) | LEL/ppm conversion, the 4 000 / 10 000 / 20 000 ppm thresholds |
| [`../docs/temperature_humidity_correction.md`](../docs/temperature_humidity_correction.md) | The optional MQDataScience T/RH compensation (model, effect, settings) |
| [`../docs/mqdatascience_comparison.md`](../docs/mqdatascience_comparison.md) | MQ-8 curve comparison and what was deliberately skipped |
| [`../.ai/instructions.md`](../.ai/instructions.md) | Rule book: lint commands, C++/Python/YAML style, domain and safety rules |
| [`components/mq_gas_sensors/README.md`](components/mq_gas_sensors/README.md) | MQ component reference (all options, calibration, behaviour) |
| [`components/mics_5524_gas_sensor/README.md`](components/mics_5524_gas_sensor/README.md) | MiCS-5524 component reference |
| [`script/README.md`](script/README.md) | Provenance of the vendored ESPHome CI linter |

## Known leftovers

* `config.yaml` still carries log-level lines for `canbus` and `toptronic` (from
  the parent project) and references a commented `packages/debug.yaml` that is
  not shipped. Add the package or delete the line.
* `documentation/` (root, **git-ignored**) holds local notes, articles and a
  reference PDF; it is not versioned, so nothing here should depend on it.

## Credits

The components embed data and formulas from MIT-licensed projects - the full
attribution is in
[`components/mq_gas_sensors/README.md`](components/mq_gas_sensors/README.md),
[`components/mics_5524_gas_sensor/README.md`](components/mics_5524_gas_sensor/README.md),
[`script/README.md`](script/README.md) and
[`../docs/README.md`](../docs/README.md):

* MQUnifiedsensor / MQSensorsLib - the PPM model of the MQ component.
* SolderedElectronics - the `a`/`b` curve table.
* MQDataScience (abcdaaaaaaaaa, MIT) - the T/RH correction and the alternative
  MQ-8 dataset.
* DFRobot_MICS (MIT) - the MiCS-5524 vendor model and its thresholds.
* ESPHome (MIT) - the vendored CI linter in `script/`.

## Licence

This repository's own code, configuration and documentation are dual-licensed
under the **Apache License 2.0** *or* the **MIT License**, at your option - see
[`../LICENSE`](../LICENSE), [`../LICENSE-APACHE`](../LICENSE-APACHE) and
[`../LICENSE-MIT`](../LICENSE-MIT).

Third-party material that is vendored, quoted or derived here keeps its own
licence and is attributed where it is used:

| Material | Where it is used | Licence |
|---|---|---|
| MQUnifiedsensor / MQSensorsLib - PPM model, and the `a`/`b` coefficients cross-checked against its examples | `components/mq_gas_sensors/` (`mq_math.h`, `coefficients.py`) | MIT |
| SolderedElectronics MQ library - `sensorConfigData.h` referenced for the curve table (the values themselves are the datasheet / MQUnifiedsensor fits) | `components/mq_gas_sensors/coefficients.py` | GPL-3.0 (data reference only) |
| MQDataScience (`MQSpaceData` v6.0.0) - temperature/humidity correction model and the alternative MQ-8 dataset | `components/mq_gas_sensors/`, `docs/mqdatascience_comparison.md` | MIT |
| DFRobot_MICS - MiCS-5524 vendor thresholds, gains and measuring ranges | `components/mics_5524_gas_sensor/coefficients.py` | MIT |
| ESPHome - `script/ci-custom.py` and `script/helpers.py`, vendored verbatim | `script/`, used by the `ci-custom` pre-commit hook | MIT |
| ESPHome framework itself (not vendored; the firmware is compiled against it like any ESPHome project) | the build | MIT for the Python codebase, GPLv3 for the C++/runtime files |
| Datasheet fits quoted from community sources (e.g. the MiCS-5524 CO two-point fit) | `docs/mics5524_conversion.md`, `docs/mq8_h2_curve.md` | facts / derivations - attributed to the sources in `docs/README.md` |

The ESP-IDF / ESP32 toolchain, the Espressif SDK and the MQ/MiCS sensor
datasheets are used under their owners' terms and are not redistributed here.

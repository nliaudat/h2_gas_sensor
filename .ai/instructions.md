# H2 gas sensor board - AI collaboration guide

**Repository:** the H2 battery-room monitoring node (ESPHome component `mq_gas_sensors`)
**Primary goal:** measure hydrogen with an MQ-8 sensor and publish a trustworthy ppm value for Home Assistant
**Hardware:** ESP32 devkit (az-delivery-devkit-v4 / nodemcu-32s) + MQ-8 module (+ optional ambient T/RH sensor, e.g. SHT4x or DHT11, linked into `packages/mq8.yaml`)
**Toolchain:** ESPHome 2026.9.0, ESP-IDF 5.5.5, C++17-compatible code, Python 3.12
**License:** Apache-2.0 OR MIT (dual, at your option - this repo); component data/credits: MQUnifiedsensor (MIT), SolderedElectronics (data), MQDataScience (MIT)

> **This file is the single source of truth for rules in this repository.**
> Generic advice ("just use std::string", "add a delay", "store it in JSON", ...) is
> overridden by the rules below.
>
> If a rule cannot be honoured, say so explicitly and explain the trade-off - do not
> silently deviate.

---

## 1. What this project is

One ESPHome device that watches hydrogen in a battery room (nickel-iron / Edison
cells) and drives a pre-alarm at 10 000 ppm (25 % of the LEL). The hard part is not
the YAML, it is the measurement chain:

```
V_ao   = adc * volt_resolution / (2^bits - 1) * voltage_multiplier
RS     = (VCC * RL) / V_ao - RL
ratio  = RS / R0                       (R0 = the value from the clean-air calibration)
PPM    = a * ratio^b                   (MQUnifiedsensor "exponential" regression)
[correction_mode: mqdatascience]       ratio_eff = ratio / (a + c * exp(b * T))
```

Quality bar for any change:

1. Both host tests pass (`esphome/tests/mq_math_test.cpp`,
   `esphome/tests/mics_math_test.cpp` - no hardware needed).
2. `esphome config` and `esphome compile` pass for `packages/mq8.yaml` (the
   fixture `tests/test_mq8_tc_package.yaml` enables its commented compensation
   from the outside, so the compensated configuration is covered too) and for
   `packages/mics5524.yaml` if the MiCS-5524 component was touched.
3. Every linter in section 3 reports clean.
4. `docs/` is updated in the same change (section 9).

A second component, `esphome/components/mics_5524_gas_sensor/`, covers the
100 - 1000 ppm trace band with its own two conversion models
(`docs/mics5524_conversion.md`). It follows the same conventions as
`mq_gas_sensors`: pure math in its own header, host tested, opt-in package, and
"the numbers in `docs/` are asserted by the host test".

---

## 2. Repository map

```
h2_gas_sensor/                      git root
├── .ai/                            this guide
├── README.md                       entry point for users (what it is, quick start)
├── docs/                           versioned project documentation (EN)
├── LICENSE                         dual-licence overview (Apache-2.0 OR MIT)
├── LICENSE-APACHE                  Apache License 2.0 (full text)
├── LICENSE-MIT                     MIT License (full text, real copyright line)
├── .gitattributes                  * text=auto eol=lf (keep LF everywhere)
└── esphome/                        the ESPHome project
    ├── config.yaml                 entry point: substitutions + package includes
    ├── readme.md                   ESPHome-side firmware reference (packages, wiring, calibration, operations)
    ├── secrets.yaml                wifi credentials - GIT-IGNORED
    ├── pyproject.toml              ruff settings (ESPHome parity)
    ├── .clang-format .clang-tidy .flake8 .yamllint .pre-commit-config.yaml
    ├── components/mq_gas_sensors/  MQ-2 ... MQ-309A component (C++ + Python codegen)
    ├── components/mics_5524_gas_sensor/  MiCS-5524 component (same layout and conventions)
    ├── packages/                   mq8.yaml (T/RH + compensation blocks commented), mics5524.yaml, board.yaml, ...
    ├── script/                     vendored ESPHome CI linter + wrapper
    └── tests/                      host tests + config fixtures
```

Rules that follow from the map:

* No tracked file may depend on a path that exists only on one machine.
* `config.yaml` still carries the log-level lines of the parent project
  (`canbus`, `toptronic`) and a commented `packages/debug.yaml` include that is
  not shipped - harmless, but delete them when you touch that file.
* The component directory name **is** the YAML platform name
  (`components/mq_gas_sensors` -> `platform: mq_gas_sensors`). Never rename one
  without the other, and never use uppercase in a component directory.
* There is exactly **one** MQ-8 package, `packages/mq8.yaml` (`id: mq8`): never add
  a variant for another T/RH sensor - the ambient sensors are linked by id
  (`temperature:`/`humidity:`), so any platform works. The T/RH sensor examples
  and the compensation keys stay commented out there, so the default build needs
  no ambient hardware and `config.yaml` keeps validating.

---

## 3. Rule book - the exact commands

Run everything from the git root unless noted. All of these must be clean before a
change is considered done.

| Rule | Command | Expected |
|---|---|---|
| ESPHome CI checks (LF, trailing whitespace, ASCII, namespace, imports, `#define`, delays, ...) | `cd esphome && python script/ci-custom.py` | **0 findings** |
| Formatting of C++ | `pre-commit run -c esphome/.pre-commit-config.yaml clang-format --all-files` (pinned **v13.0.1**; the local `clang-format` 22.x formats differently) | no changes |
| YAML | `cd esphome && yamllint -c .yamllint .` | no output |
| Python (flake8) | `cd esphome && flake8 --config .flake8 components tests script` | no output |
| Python (ruff) | `cd esphome && ruff check . && ruff format --check .` | "All checks passed!" / "already formatted" |
| Host test | `cd esphome/tests && g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mq_gas_sensors mq_math_test.cpp -o mq_math_test.exe && mq_math_test.exe` | "All mq_math tests passed." and no compiler warning |
| Host test (MiCS-5524) | `cd esphome/tests && g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test.exe && mics_math_test.exe` | "All mics_math tests passed." and no compiler warning |
| Config validation | `cd esphome && esphome config config.yaml` | "Configuration is valid!" |
| Build | `cd esphome && esphome compile config.yaml` | "Successfully compiled program." |

Notes and gotchas:

* `pre-commit` must be invoked with `-c esphome/.pre-commit-config.yaml` because the
  git root is the parent directory of the ESPHome project. Its
  `no-commit-to-branch` hook forbids commits on `main`/`dev`/`master` - the human's
  concern only: the AI does not commit at all (section 10).
* `script/ci-custom.py` and `script/helpers.py` are **vendored verbatim** (MIT) from
  `esphome/esphome`; `script/run_ci_custom.py` is the local wrapper that runs the
  linter with `esphome/` as CWD. That CWD matters: upstream only performs the
  namespace and component-relative-import checks when the paths it receives start
  with `components/`. Do not "fix" the vendored files - update them from upstream
  (see `esphome/script/README.md`).
* The `mixed-line-ending --fix=lf`, `end-of-file-fixer` and `trailing-whitespace`
  hooks from `.pre-commit-config.yaml` overlap with `ci-custom`; both must pass.
* Never commit `esphome/tests/mq_math_test.exe` (ignored) or `secrets.yaml`.

---

## 4. C++ rules

**Baseline:** Google C++ style as adapted by ESPHome, formatted by clang-format
v13.0.1 with `esphome/.clang-format` (2-space indent, 120 columns, attached braces,
`PointerAlignment: Right`, `SortIncludes: false`). If clang-format disagrees with
your hand-written layout, clang-format wins.

| Aspect | Rule |
|---|---|
| Language level | C++17. `mq_math.h` must stay buildable with `g++ -std=c++17 -Wall -Wextra` and must not include ESPHome/ESP-IDF headers. |
| Names | types `UpperCamelCase`, functions/methods/variables `lower_snake_case`, constants `UPPER_SNAKE_CASE`, fields `lower_snake_case_with_trailing_underscore_`. |
| Member access | every member/method call is prefixed with `this->`. |
| Visibility | `protected` for fields; `private` only for true implementation details; setters are inline one-liners in the header. |
| Constants | `static constexpr` (never `#define`; `#pragma once` is the only allowed preprocessor directive). |
| Casts | `static_cast` (never C casts); avoid `reinterpret_cast`. |
| Forbidden | `byte`, `std::string_view`, `sprintf`/`scanf`, `std::bind`, `std::to_string`, heap-allocating ESPHome helpers (`format_hex`, `get_mac_address`, ...), Arduino-only APIs, `delay(<literal> >= 50)`, `ESP_LOG*` inside a header, absolute includes (`#include "components/.../x.h"`). |
| Includes | relative, same directory: `#include "mq_math.h"`, `#include "mq_gas_sensor.h"`. |
| Namespace | `namespace esphome::mq_gas_sensors { ... }` - it must match the component directory name. |
| Logging | `ESP_LOGCONFIG` in `dump_config()`/`log_config_()`, `ESP_LOGI`/`ESP_LOGD`/`ESP_LOGW`/`ESP_LOGE` in the `.cpp` only, always with `this->type_.c_str(), this->gas_.c_str()` as a prefix so multi-sensor logs are readable. |
| Numbers | no magic numbers: name them (`MQ_TC_RH_MIN`, `R0_PREFERENCE_VERSION`, ...); use two-digit precision in log format strings. |
| Structure | pure math (no side effects, no state) in `mq_math.h`; everything stateful (sampling, calibration, publishing, flash) in `MQGasSensor`. |
| Floats | compute in `double` where the result is compared or logged, publish `float`; guard every division and `pow`/`log` (`will_overflow`, `isfinite`). |

Component skeleton to preserve: `Sensor + PollingComponent`, `setup()`, `loop()`,
`update()`, `dump_config()`, `get_setup_priority() = setup_priority::DATA`,
`mark_failed()` on unusable configuration.

---

## 5. Python rules (config / code generation)

Formatted and linted by **ruff** (settings in `esphome/pyproject.toml`, mirroring
ESPHome) plus **flake8** (`.flake8`). 4-space indent, absolute `esphome.*` imports,
relative imports **inside** the component (`from . import ...`,
`from .coefficients import ...`) - never `from components.mq_gas_sensors import ...`.

| Rule | Detail |
|---|---|
| Validate at config time | every user error must fail during `esphome config` with `cv.Invalid`, never at runtime on the device. |
| Error messages | name the offending value, list the valid alternatives (`expected one of: ...`, `available gases: ...`), and suggest the fix when it is obvious. |
| Cross-checks | put them in `_validate_config()` (`cv.All(..., _validate_config)`); examples already implemented: `max_ppm > min_ppm`, `r0:` vs `calibration:`, `correction_mode` needs both `temperature:`/`humidity:`, `curve:` forbids explicit `a:`/`b:`. |
| Warnings | use `_LOGGER.warning` for "valid but probably not what you want" (e.g. both `r0:` and `calibration:`), `_LOGGER.info` for resolved-but-non-default choices. |
| Schema style | `sensor.sensor_schema(...).extend({...}).extend(cv.polling_component_schema("60s"))`; `cv.Optional(CONF_X, default=...)` with an explicit type/validator; `cv.one_of(*MAP, lower=True)` for enum-like keys backed by a module-level `dict` (see `REGRESSION_METHODS`, `CORRECTION_MODES`, `RATIO_MODES`). |
| Constants | `CONF_*` keys live in `__init__.py`; data tables and helpers in `coefficients.py`; keep both free of ESPHome imports where possible. |
| `to_code()` | `cg.add(var.set_x(...))` only; no logic that can fail after validation (the one defensive `raise` there is marked `# pragma: no cover`). |
| Documentation | module and public function docstrings (Google style), rST double backticks for code in docstrings. |
| API stability | adding an optional key is fine; renaming/removing a key or changing a default is a **breaking change** and must be documented in `docs/` and called out in the summary. |
| Names | `snake_case` for keys and functions; keys are user facing, keep them short (`rl`, `r0`, `vcc`, `curve`). |

---

## 6. YAML rules

Linted by `yamllint` with `esphome/.yamllint`: LF, 2-space indent, at most one
consecutive blank line, no trailing whitespace, `%` never quoted as truthy, long
lines allowed.

* `esphome/config.yaml` holds only `substitutions`, `external_components`, logging,
  includes and one-line toggles; real content goes into `esphome/packages/*.yaml`.
* `external_components` lists the component folders explicitly
  (`components: [mq_gas_sensors, mics_5524_gas_sensor]`): a new folder under
  `esphome/components/` must be added there, it is not loaded automatically.
* `packages/mq8.yaml` is the single MQ-8 package and the only place that defines
  `id: mq8`: the optional T/RH sensor and the MQDataScience compensation live in
  it as commented blocks (linked by id), so no second package is ever needed and
  a shared option changes once. `id: mq8` must never be redefined elsewhere.
* Use substitutions for anything board specific (`mq8_pin`, `mq8_multiplier`,
  `mq8_rl`, `mq8_i2c_sda`, `mq8_dht_pin`, ...) and document them in the file
  header comment. A substitution that only the commented examples use is allowed
  to stay unused.
* Keep the header comment of a package accurate: wiring, calibration workflow,
  threshold references and the link to the matching document under `docs/`.
* `platform:` must match the component directory name (`mq_gas_sensors`).
* Every package must survive `esphome config` **and** `esphome compile`.

---

## 7. Domain rules (measurement, calibration, safety)

### 7.1 Measurement

* `mq_math.h` is the only place where the chain `V -> RS -> ratio -> PPM` is
  implemented; `MQGasSensor` only feeds it. Reimplementing a formula elsewhere is
  forbidden - extend the header and add a host test instead.
* `RS = (VCC * RL) / V - RL` needs the *real* load resistor of the module: the
  documented `rl:` of a cheap breakout is often 1 kOhm instead of 10 kOhm.
* The ADC sees the divided output: `voltage_multiplier` is the inverse of the
  divider ratio. Never "fix" a wrong divider by editing `vcc` or `rl`.
* `ratio_mode: rs_r0` is the default and matches the published `a`/`b`; only switch
  to `r0_rs` with coefficients fitted for it (`docs/mq8_h2_curve.md`).
* Model the divider explicitly (`divider: {r1, r2}` in kOhm - r1 in series, r2 to
  ground - or the low-level `voltage_multiplier`) and let the guard check it:
  `vcc / voltage_multiplier` must stay below `adc_pin_max` (3.6 V on an ESP32; the
  ADC pins are **not** 5 V tolerant) and is warned above `adc_input_max` (3.3 V).
  **10k/20k (-> 1.5) is the project default**; the packages declare
  `adc_input_max: 3.33` for it. Never wire a 5 V output straight to a GPIO.

### 7.2 Calibration

* `R0 = RS_air / ratio_in_clean_air` (70 for the MQ-8). Calibrate in **clean air**
  after the heater stabilised; a brand-new sensor needs 24-48 h burn-in
  (`warmup_time`).
* Only calibrate when `R0` is unknown (first boot, cleared flash or
  `persist: false`). Never recalibrate automatically while hydrogen may be present.
* A manual request (`request_calibration()`, wired to the *recalibrate* buttons of
  the packages) is **deferred until `warmup_time` has elapsed** - a cold sensor
  would capture a wrong reference. The state stays `unknown` while a calibration
  is pending or running, because the calibration changes the reference: a value
  computed with the previous `R0`/`x_air` must never stay visible.
* The calibration path is *uncorrected*: the T/RH correction is applied to later
  readings, exactly like MQDataScience.

### 7.3 Safety (a battery room is not a lab bench)

* Invalid readings (AO <= 10 mV, open circuit, missing 5 V, no `R0`) publish
  `unknown` - **never** `0 ppm`, so a broken sensor cannot look like clean air.
* Auxiliary inputs fail *open*: a missing/stale/NaN temperature or humidity reading
  leaves the correction at 1.0 and logs a one-time warning; it must never suppress
  or zero the gas measurement.
* `correction_clamp: absolute` (default) keeps `max_ppm` as the alarm ceiling;
  `scaled` reproduces MQDataScience's `max_ppm * correction` and is documented as
  *less* safe.
* The 10 000 ppm pre-alarm equals 25 % of the H2 LEL and is the design target
  (NFPA 855). Never raise a ceiling or silence a warning without saying so.
* Alerting belongs in Home Assistant, not in the component.

### 7.4 Honesty about accuracy

The MQ-8 is a coarse log-log fit of a datasheet plot with a +/-30 % sensor-to-sensor
spread, drift and cross-sensitivity (alcohol, LPG, CH4, CO). Never describe the ppm
value as accurate, calibrated to a standard, or suitable for explosive-range
measurement. `docs/mqdatascience_comparison.md` records the same-curve ambiguity
(~11 % anchor difference) - keep such caveats when touching the curve data.

### 7.5 Provenance / attribution

When adding data or formulas, update the Credits in
`esphome/components/mq_gas_sensors/README.md` and the sources table in
`docs/README.md`: MQUnifiedsensor/MQSensorsLib and MQDataScience are MIT, the
SolderedElectronics curve table is a GPL-3.0 data reference. Vendored ESPHome
scripts stay unmodified and are credited in `esphome/script/README.md`.

---

## 8. Testing rules

* **Host test first.** Any change to a math header, the coefficients or the
  correction constants needs assertions in `esphome/tests/mq_math_test.cpp` (MQ)
  or `esphome/tests/mics_math_test.cpp` (MiCS-5524), with expected values
  recomputed from first principles (never copied from the implementation).
* Keep the test building with `-Wall -Wextra` and zero warnings.
* Config-only changes: run `esphome config` for `config.yaml`, `tests/test_no_id.yaml`
  and `tests/test_tc.yaml`; the negative cases (missing `temperature:`, `curve:`
  plus `a:`/`b:`, unsupported correction type) must still be rejected at config time.
* Runtime changes: additionally `esphome compile` for `config.yaml` and for
  `tests/test_mq8_tc_package.yaml`. That fixture includes `packages/mq8.yaml` and
  enables its commented compensation from the outside (`id: !extend mq8` plus two
  template sensors), so the compensated package is compiled without editing
  `config.yaml` and without duplicating the package.
* Never commit the compiled host test binary.
* Tests must not require hardware, network or secrets.

---

## 9. Documentation rules

* `docs/` is the versioned, user-facing documentation; `docs/README.md` is the index.
  The user-facing set is `../README.md` (entry point), `getting_started.md` (setup),
  `home_assistant_alerts.md` (usage), `troubleshooting.md` and `development.md`, plus
  the topic documents (`mq8_*`, `mics5524_*`, `h2_thresholds`, `temperature_humidity_*`).
* Every number quoted in `docs/` must be asserted by `mq_math_test.cpp`; when a
  constant changes, update code, test and docs in the same change.
* Markdown may use Unicode (arrows, multiplication signs, micro, degrees - the ASCII
  rule covers code and config files only), but keep it ASCII where it is easy.
* Docs must never rely on a path that only exists on one machine (git-ignored
  scratch folders, absolute paths): a fresh clone has to be enough, and every
  number needs a public source.
* Component API/options are documented in
  `esphome/components/mq_gas_sensors/README.md`; project-level topics (hardware,
  thresholds, curve provenance, comparisons) in `docs/`.

---

## 10. Git, workflow and AI behaviour

* **The AI never commits - a human reviews and commits.** No `git commit`, no
  `git push`, no `git merge`/`rebase`/`cherry-pick`/`reset`, no tag, no branch
  deletion: whatever the instruction, whatever the size of the change, even when
  the pre-commit hooks are not installed. Staging is allowed (`git add`) because
  the vendored linter only sees tracked files, and it is reversible with
  `git restore --staged`. The AI leaves the work in the working tree, lists every
  file it touched and how it was verified, and stops there. The human reviews
  (`git status`, `git diff --cached`) and creates the commit.
* The human commits on a **branch** (`no-commit-to-branch` blocks
  `main`/`dev`/`master`); commit messages are imperative, one concern, and
  reference a document when the change is data/provenance related.
* Before anything that can move the git state - creating or switching a branch,
  `checkout`, `stash`, a tool that restores a checkpoint - copy the pending
  changes to a scratch folder outside the repository, or ask the human to commit
  first. Uncommitted work has already been wiped that way once in this repository.
* Before saying a task is done: run the section 3 commands, re-read the changed
  files, and report the results (command -> result), including anything that could
  not be verified locally.
* Report deviations explicitly ("rule X is not satisfied because ...").
* Keep `esphome/.esphome/` (build cache) out of git; it is ignored.

---

## 11. Reject list (fast review)

| Never | Instead |
|---|---|
| CRLF, tabs, non-ASCII in code/YAML, trailing whitespace, missing final newline, two consecutive blank lines in YAML | LF + ASCII + single blank line (the hooks fix or fail on these) |
| `#define` constant, magic number | `static constexpr` with a name |
| C cast, `byte`, `std::string_view`, `sprintf`, `std::to_string`, `delay(500)` | `static_cast`, `uint8_t`, `const char *`, `snprintf`/`format_..._to`, non-blocking state machine |
| `ESP_LOG*` in a header, absolute include, wrong namespace | logging in the `.cpp`, relative include, `namespace esphome::mq_gas_sensors` |
| Publishing `0 ppm` for a broken/unknown reading | publish `unknown` (NaN) and log a warning |
| Recalibrating `R0` while R0 is known | only calibrate on first boot or on request |
| Silently changing a default, a clamp or a threshold | opt-in new key + docs + summary note |
| Copying a curve constant into a doc without a test | assert it in `mq_math_test.cpp` and cite it in `docs/` |
| Vendored file edited to silence a lint | update from upstream or exclude it explicitly |
| Committing build artifacts or secrets | keep them ignored (`mq_math_test.exe`, `secrets.yaml`) |
| `git commit`, `git push`, `git merge`/`rebase`, `git reset`, branch deletion, a tag | leave the changes in the working tree and report them; the human reviews the diff and commits (section 10) |

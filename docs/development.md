# Development - validate, build, test, lint

The full rule book (C++/Python/YAML style, domain and safety rules, review
checklist) is [`../.ai/instructions.md`](../.ai/instructions.md); its section 3
lists the commands below as the "must be clean before a change is done" set.

## Layout in one paragraph

`esphome/config.yaml` is the entry point (substitutions + package includes),
`esphome/packages/*.yaml` are the includes, `esphome/components/` holds the two
custom components (`mq_gas_sensors`, `mics_5524_gas_sensor`) with their own
READMEs, `esphome/tests/` holds the host tests and the config fixtures, and
`esphome/script/` holds the vendored ESPHome CI linter (see
[`../esphome/script/README.md`](../esphome/script/README.md)).

A new component must also be listed in `config.yaml`
(`external_components: ... components: [...]`): folders under `esphome/components/`
are not discovered automatically.

## Validate configurations (no hardware needed)

```bash
cd esphome
esphome config config.yaml                     # the project configuration
esphome config tests/test_no_id.yaml           # MQ-8 without id:, pin: sugar, fixed r0
esphome config tests/test_tc.yaml              # T/RH correction + curve: mqdatascience
esphome config tests/test_mq8_sht4x_package.yaml   # the SHT4x package
esphome config tests/test_mq8_dht11_package.yaml   # the DHT11 package
esphome config tests/test_mics.yaml            # MiCS: both conversion models, divider, ADS1115 case
esphome config tests/test_mics_package.yaml    # the shipped MiCS package
python tests/inspect_config.py tests/test_no_id.yaml   # show the resolved id
```

Negative cases (`esphome config` must fail with an explanatory message) are part
of the fixtures too - for example a 5 V module wired straight to an ADC pin.

## Build and flash

```bash
cd esphome
esphome compile config.yaml     # full ESP-IDF build
esphome run config.yaml         # flash over USB or OTA
```

## Host tests (the measurement math)

The math of both components lives in a header with no ESPHome/ESP-IDF
dependency, so it compiles and asserts on the host:

```bash
cd esphome/tests
g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mq_gas_sensors mq_math_test.cpp -o mq_math_test.exe && mq_math_test.exe
g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test.exe && mics_math_test.exe
```

Every number quoted in `docs/` is asserted there: change a curve, a coefficient
or a threshold and the test changes with it. `-Wall -Wextra` must stay
warning-free.

## Lint

```bash
cd esphome
python script/ci-custom.py                     # ESPHome's own checks (LF, namespace, imports, ...)
yamllint -c .yamllint .
flake8 --config .flake8 components tests script
ruff check . && ruff format --check .

# from the git root; the version is pinned (clang-format v13.0.1)
pre-commit run -c esphome/.pre-commit-config.yaml clang-format --all-files
```

`script/ci-custom.py` and `script/helpers.py` are vendored verbatim from
`esphome/esphome` (MIT) - do not edit them locally, update them from upstream
([`../esphome/script/README.md`](../esphome/script/README.md)).

## Adding a gas or a curve

The MQ component already covers MQ-2 ... MQ-309A and their gases: check
[`../esphome/components/mq_gas_sensors/README.md`](../esphome/components/mq_gas_sensors/README.md)
for the `sensor_type` / `gas` / `curve` keys and the coefficient table before
adding anything. A new constant has to be asserted in the host test and cited in
`docs/`.

## Workflow

* **AI agents never commit.** Every change is left in the working tree, listed file
  by file, and reviewed (then committed) by a human.
* Work on a **branch** - `main` is protected by the `no-commit-to-branch` hook.
* `docs/` is the user-facing documentation: update it in the same change as the
  behaviour it describes.
* Never commit build artifacts or secrets (`esphome/.esphome/`, `tests/*.exe`,
  `esphome/secrets.yaml`).

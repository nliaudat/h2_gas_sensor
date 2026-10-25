# Development - validate, build, test, lint

The full rule book (C++/Python/YAML style, domain and safety rules, review
checklist) is [`../.ai/instructions.md`](../.ai/instructions.md); its section 3
lists the commands below as the "must be clean before a change is done" set.

## Layout in one paragraph

`esphome/config.yaml` is the entry point (substitutions, package includes and the
`external_components:` block), `esphome/packages/*.yaml` are the includes,
`esphome/tests/` holds the host tests, the vendored math headers and the config
fixtures, `docs/components/` holds the component references, and `esphome/script/`
holds the vendored ESPHome CI linter (see
[`../esphome/script/README.md`](../esphome/script/README.md)).

None of the three components lives in this repository: `mq_gas_sensors` and
`tsdb` are fetched from their pull requests (see below) and `mics_5524` is
merged into ESPHome. A new component has to be listed in the
`external_components:` block of `config.yaml` - nothing is discovered
automatically.

## Upstream revisions

The build tracks the PR heads (`github://pr#...` cannot be pinned to a commit),
so the state the documentation and the host tests were written against is
recorded here. `refresh: always` re-fetches on every `esphome config`/`compile`.

| Component | Source | Revision this tree was verified against |
|---|---|---|
| `mq_gas_sensors` | `github://pr#20896` | `320bab784c7fbeb1cc0509855f10473394a7fba5` |
| `tsdb` | `github://pr#20892` | `40b8d412def3b607013bc61a6765554eada28444` |
| `mics_5524` | merged into ESPHome (PR #21024) | ESPHome `2026.11.0-dev` as installed |
| `wifi` `ap_mode:` | merged into ESPHome (PR #20077) | ESPHome `2026.11.0-dev` as installed |

The math of each component is compiled from a **vendored copy** of its header
next to the host test (`esphome/tests/{mq,mics,tsdb}_math.h`, each with the
revision in its banner), so a host test run always asserts the code the build
uses. When a PR head changes a header, re-copy it, re-run the tests and update
the numbers in `docs/` - the banner is the only local addition.

`mq_gas_sensors` needs one local patch on this hardware: the PCB feeds both gas
sensors through a 10k/20k divider (5 V -> 3.33 V), and the current PR head
refuses a declared `adc_input_max:` above the ADC's 2.45 V measuring range.
[`../patches/pr20896-adc-input-max.patch`](../patches/pr20896-adc-input-max.patch)
(`git apply` in an `esphome/esphome` checkout at the revision above) makes the
declared range authoritative again, the way the merged `mics_5524` component
treats it - see [`../patches/README.md`](../patches/README.md). **Until it is
applied, `esphome config config.yaml` fails in `packages/mq8.yaml`** with
"the sensor output can reach 5.00 V and the configured wiring maps that to
3.33 V at the ADC input".

## Validate configurations (no hardware needed)

```bash
cd esphome
esphome config config.yaml                     # the project configuration
esphome config tests/test_no_id.yaml           # MQ-8 without id:, pin: sugar, fixed r0
esphome config tests/test_tc.yaml              # T/RH correction + curve: mqdatascience
esphome config tests/test_mq8_tc_package.yaml      # the MQ-8 package with its compensation enabled
esphome config tests/test_mics.yaml            # MiCS: both conversion models, voltage_divider, ADS1115 case
esphome config tests/test_mics_package.yaml    # the shipped MiCS package
esphome config tests/test_web_package.yaml     # the offline webserver package (open AP + dashboard)
esphome config tests/test_tsdb.yaml            # the history package on stand-in sensors
python tests/inspect_config.py tests/test_no_id.yaml   # show the resolved id
```

Negative cases (`esphome config` must fail with an explanatory message) are part
of the fixtures too - for example a 5 V module wired straight to an ADC pin; the
ones of the history component (`scale: 0`, a duplicate column, a
`max_file_size`/`partition_size` mismatch, `require_time` without `time_id`, an
unaligned partition, `memory: psram` without `psram:`) are listed in the header
of [`../esphome/tests/test_tsdb.yaml`](../esphome/tests/test_tsdb.yaml).

## Build and flash

```bash
cd esphome
esphome compile config.yaml     # full ESP-IDF build
esphome run config.yaml         # flash over USB or OTA
```

`esphome compile` ends with the RAM/flash summary; for the per-archive and
per-object breakdown (what the history feature costs, which object grew after a
change) run ESP-IDF's size tool from the ESPHome-installed IDF environment
(`<ESPHome data dir>/Cache/idf/penvs/<idf version>/Scripts/python.exe` on
Windows) on the map file of the last build:

```bash
python -m esp_idf_size --archives .esphome/build/h2_sensor/build/h2_sensor.map
python -m esp_idf_size --files    .esphome/build/h2_sensor/build/h2_sensor.map
```

The reference values of the shipped configuration are in
[`data_logging.md`](data_logging.md#flash-and-ram-cost-measured-baseline); refresh
that table in the same change when a build shifts them noticeably.

## Host tests (the measurement math)

The math of the three components lives in a header with no ESPHome/ESP-IDF
dependency, vendored next to the tests, so it compiles and asserts on the host:

```bash
cd esphome/tests
g++ -std=c++17 -O2 -Wall -Wextra -I . mq_math_test.cpp -o mq_math_test.exe && mq_math_test.exe
g++ -std=c++17 -O2 -Wall -Wextra -I . mics_math_test.cpp -o mics_math_test.exe && mics_math_test.exe
g++ -std=c++17 -O2 -Wall -Wextra -I . tsdb_math_test.cpp -o tsdb_math_test.exe && tsdb_math_test.exe
```

Every number quoted in `docs/` is asserted there: change a curve, a coefficient,
a threshold, a `scale` or the file geometry of the history and the test changes
with it (the capacity/retention table of
[`data_logging.md`](data_logging.md) is printed by the tsdb test).
`-Wall -Wextra` must stay warning-free.

## Lint

```bash
# from the git root: every hook at once (clang-format is pinned to v13.0.1)
pre-commit run -c esphome/.pre-commit-config.yaml --all-files

# the same checks one by one, to narrow one down
python esphome/script/run_ci_custom.py          # ESPHome's own checks (LF, namespace, imports, ...)
cd esphome && yamllint -c .yamllint . && flake8 --config .flake8 tests script
cd esphome && ruff check . && ruff format --check .
```

`pre-commit run --all-files` stops on the `no-commit-to-branch` hook while the
branch is `main`, `dev` or `master`; prefix the command with
`SKIP=no-commit-to-branch` to run the checks there.

`ci-custom.py` and `helpers.py` are vendored verbatim from `esphome/esphome`
(MIT) - do not edit them locally, update them from upstream
([`../esphome/script/README.md`](../esphome/script/README.md)). The wrapper
`script/run_ci_custom.py` is the only local file: it runs the linter from the git
root, hands it an empty `esphome/const.py` (this repository ships none, so the two
upstream checks backed by that file are inactive) and keeps the vendored files
and `pcb/` out of its file list.

## Adding a gas or a curve

The MQ component already covers MQ-2 ... MQ-309A and their gases: check
[`components/mq_gas_sensors.md`](components/mq_gas_sensors.md)
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

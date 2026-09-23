# script/

Vendored copies of ESPHome's CI linter, used by the `ci-custom` pre-commit hook
in [`../.pre-commit-config.yaml`](../.pre-commit-config.yaml).

| File | Origin |
|---|---|
| `ci-custom.py` | <https://github.com/esphome/esphome/blob/dev/script/ci-custom.py> |
| `helpers.py` | <https://github.com/esphome/esphome/blob/dev/script/helpers.py> |
| `run_ci_custom.py` | local wrapper (see below) |

Vendored revision: `esphome/esphome@91ab2290` (`dev` branch, 2026-09-17).

Both vendored files are unmodified copies. ESPHome publishes its Python code
under the **MIT** license (only the C++/runtime files are GPLv3), see
<https://github.com/esphome/esphome/blob/dev/LICENSE>.

## Running the checks

```bash
cd esphome
python script/ci-custom.py                     # every check, every file
python script/ci-custom.py components packages # regex filter on the paths
```

The pre-commit hook runs `run_ci_custom.py`, which changes into `esphome/`
before executing the linter: upstream derives the expected namespace and the
component-relative-import rules from the paths it receives, so those checks are
only active when it is started from the ESPHome project directory (paths must
begin with `components/`).

## Updating

Copy the current `ci-custom.py` / `helpers.py` from `esphome/esphome` over the
local ones (no local modifications) and re-run the checks.

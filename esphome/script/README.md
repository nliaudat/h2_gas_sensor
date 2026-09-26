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
# from the git root; the same command the `ci-custom` hook runs
python esphome/script/run_ci_custom.py              # every check, every file
python esphome/script/run_ci_custom.py components   # regex filter on the paths
```

The wrapper changes into the git root before executing the linter: upstream
derives the expected namespace and the component-relative-import rules from the
paths it receives, so those checks are only active when the paths look like
`esphome/components/...`.

It also shields two things from the linter:

* the vendored files themselves - upstream's own `ci-custom.py` excludes
  `script/*` because it does not lint its own tooling, and that exclusion cannot
  match this deeper `esphome/script/` copy;
* `pcb/` - the binary hardware archives, a file type upstream's checks do not
  cover.

`ci-custom.py` reads upstream's `esphome/const.py` when it starts, a file this
repository does not have; the wrapper serves it an empty one, so the two checks
backed by that file (a constant already defined in `const.py`, and the frozen
`CONST_PY_MAX_CONF` counter) stay quiet. Every other check is unaffected.

## Updating

Copy the current `ci-custom.py` / `helpers.py` from `esphome/esphome` over the
local ones (no local modifications) and re-run the checks.

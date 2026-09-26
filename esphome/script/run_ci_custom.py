"""Run the vendored ESPHome ``ci-custom.py`` against this repository.

Upstream derives some checks (namespace, component-relative includes/imports)
from the paths it is handed, so the linter has to see the layout it was written
for: the ``esphome/`` project directory holding ``components/``, ``packages/``
and ``tests/``, with paths expressed relative to the git root
(``esphome/components/...``). ``pre-commit`` starts hooks in the git root, but a
manual run can come from anywhere, so this wrapper changes the directory first.

The vendored linter also reads upstream's ``esphome/const.py`` at import time
(it feeds the "constant is already defined in const.py" and the frozen
``const.py`` checks). This repository does not ship upstream's ``const.py``, so
the wrapper hands the linter an empty one: those two checks stay quiet and
every other check runs unchanged.

Two path families are hidden from the linter's file list, see
``_install_lint_exclusions``: the vendored linter itself (upstream's own
``exclude=["script/*"]`` entries cannot match our deeper ``esphome/script/``)
and the binary ``pcb/`` hardware assets, which upstream's file-type rules do
not cover.

Equivalent manual invocation::

    python esphome/script/run_ci_custom.py [path-regex ...]

    # from the git root, through pre-commit:
    pre-commit run -c esphome/.pre-commit-config.yaml ci-custom --all-files

See ``script/README.md`` for the provenance of the vendored files.
"""

import codecs
import io
import os
from pathlib import Path
import re
import runpy
import sys

GIT_ROOT = Path(__file__).resolve().parents[2]
CI_CUSTOM = Path(__file__).resolve().parent / "ci-custom.py"

# File the vendored linter opens at import time (see the module docstring).
UPSTREAM_CONST_PY = "esphome/const.py"

# Paths hidden from the linter, see ``_install_lint_exclusions``.
LINT_EXCLUDED_PATHS = re.compile(r"^(?:esphome/script/|pcb/)")

_REAL_CODECS_OPEN = codecs.open


def _open_with_missing_const_py(path, *args, **kwargs):
    """Serve an empty ``esphome/const.py`` when the upstream file is absent."""
    if path == UPSTREAM_CONST_PY and not (GIT_ROOT / UPSTREAM_CONST_PY).exists():
        return io.StringIO("")
    return _REAL_CODECS_OPEN(path, *args, **kwargs)


def _install_lint_exclusions() -> None:
    """Drop paths from the file list the vendored linter works on.

    ``ci-custom.py`` excludes its own tooling (``script/*``) because upstream
    keeps the linter at the repository root and does not lint it; our vendored
    copy sits one level deeper in ``esphome/script/``, so those upstream exclude
    patterns cannot match it and it would otherwise be linted as project code.
    The ``pcb/`` directory holds the hardware archives, a file type upstream's
    checks do not know (it tracks no binary CAD assets).

    The linter calls ``helpers.git_ls_files()`` to build its file list, so
    filtering that function filters every check, including the ones that look at
    ``esphome/components/`` and ``esphome/tests/`` directly.
    """
    import helpers

    unfiltered_git_ls_files = helpers.git_ls_files

    def git_ls_files(patterns=None):
        """Return ``git ls-files`` without this repository's tooling and PCB files."""
        return {
            path: mode
            for path, mode in unfiltered_git_ls_files(patterns).items()
            if not LINT_EXCLUDED_PATHS.match(path)
        }

    helpers.git_ls_files = git_ls_files


def main() -> None:
    """Run ci-custom.py with the git root as the working directory."""
    os.chdir(GIT_ROOT)
    codecs.open = _open_with_missing_const_py
    _install_lint_exclusions()
    sys.argv = [str(CI_CUSTOM), *sys.argv[1:]]
    runpy.run_path(str(CI_CUSTOM), run_name="__main__")


if __name__ == "__main__":
    main()

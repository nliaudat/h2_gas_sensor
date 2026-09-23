"""Run the vendored ESPHome ``ci-custom.py`` against this repository.

Upstream resolves some checks (namespace, component relative includes/imports)
from the file paths it is handed, so it must run with the ESPHome project
directory (``esphome/``) as the working directory and see paths relative to it
(``components/...``). ``pre-commit`` starts hooks in the git root instead, so
this wrapper changes the directory first.

Equivalent manual invocation::

    cd esphome && python script/ci-custom.py

See ``script/README.md`` for the provenance of the vendored files.
"""

import os
from pathlib import Path
import runpy
import sys

ESPHOME_DIR = Path(__file__).resolve().parent.parent
CI_CUSTOM = ESPHOME_DIR / "script" / "ci-custom.py"


def main() -> None:
    """Run ci-custom.py with ``esphome/`` as the working directory."""
    os.chdir(ESPHOME_DIR)
    sys.argv = [str(CI_CUSTOM)]
    runpy.run_path(str(CI_CUSTOM), run_name="__main__")


if __name__ == "__main__":
    main()

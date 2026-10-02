"""Repair tool for the `littlefs` partition (stale bytes from an old partition table).

Inspect - and optionally erase - the stale bytes that an *older* partition table
left inside today's `littlefs` region. `esphome run` and OTA only write the
bootloader, the partition table, `otadata` and the app, so those bytes survive
every re-flash and `tsdb` can never mount (a partition that is not all 0xFF is
never formatted on purpose - that would discard history).

Run it from the repo root. Usage - this board does *not* enter download mode from
esptool's DTR/RTS dance (the ROM only listens for the host when it boots with
GPIO0 low), so put it there first, in this order:

    hold BOOT, tap EN (or unplug/replug the USB cable), release BOOT
    # the chip now waits in the serial bootloader, with no timing pressure

    python tools/tsdb_partition_fix.py            # read the region and report (always safe)
    python tools/tsdb_partition_fix.py --erase    # ... then erase it and verify

Every call uses `--before no-reset` (the chip is already in download mode and
answers immediately). The reset happens exactly once, at the very end (`--after
hard-reset`), because this board *is* reset by RTS: the app then boots and
formats the freshly erased partition on that first boot. If the board does not
come back on its own, press EN or replug the USB cable - the erase has run by
then, so nothing is lost.

`--erase` refuses to touch a region that holds a real LittleFS (the `littlefs`
magic in the superblock), so a working history cannot be destroyed by accident.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

# ESPHome's generated table: nvs 0x310000-0x380000, littlefs 0x380000-0x400000
PORT = "COM9"
ADDRESS = 0x380000
SIZE = 0x80000
# The old table kept `nvs` at 0x390000 and ended `app1` there, so the first
# 0x18000 bytes are the part that matters for the diagnosis.
HEAD = 0x18000
BLOCK = 4096


def esptool(*args: str, after: str = "no-reset") -> None:
    """Run esptool with the options this board needs (`--before no-reset`).

    `after` stays `no-reset` for every step but the last: RTS *does* reset this
    board, so a reset in the middle of the sequence would drop the chip out of the
    bootloader (into the app, which formats the partition) before the next step
    could talk to it.
    """
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32",
        "--port",
        PORT,
        "--before",
        "no-reset",
        "--after",
        after,
        *args,
    ]
    print(">", " ".join(command), flush=True)
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError:
        # esptool's auto-reset cannot reach the ROM bootloader on this board (only
        # RTS is wired, not the GPIO0 side), so a connect attempt stalls for ~30 s
        # and then exits with a bare code. Say what to do instead.
        raise SystemExit(
            f"\nesptool could not talk to the chip on {PORT}.\n"
            "Put it into download mode first: hold BOOT, tap EN (or unplug/replug the USB\n"
            "cable), release BOOT - the ROM then waits for this script. Then re-run the same\n"
            "command: every step before the erase only reads, and the erase is idempotent."
        ) from None


def read_region(address: int, size: int, after: str = "no-reset") -> bytes:
    """Read `size` bytes at `address` (needs the chip in download mode)."""
    path = Path(tempfile.gettempdir()) / "tsdb_region.bin"
    esptool("read-flash", hex(address), hex(size), str(path), after=after)
    data = path.read_bytes()
    path.unlink(missing_ok=True)
    if len(data) != size:
        # esptool creates the output file before it connects, so a failed connect
        # leaves an empty file behind - never let that pass as a measurement.
        raise SystemExit(
            f"esptool returned {len(data)} bytes instead of {size} - the read did not run; "
            "put the chip into download mode (see above) and try again. Nothing was changed."
        )
    return data


def report(data: bytes, address: int) -> int:
    """Print the non-0xFF bytes per 4 KB block; return how many there are."""
    stale = 0
    for offset in range(0, len(data), BLOCK):
        block = data[offset : offset + BLOCK]
        non_ff = sum(1 for byte in block if byte != 0xFF)
        if non_ff:
            stale += non_ff
            print(
                f"  0x{address + offset:06X}: {non_ff:4d} of {BLOCK} bytes are not 0xFF"
            )
    if stale:
        print(
            f"  {stale} stale bytes - an older partition table still owns this region, "
            f"so it will never format itself"
        )
    else:
        print(
            "  entirely 0xFF (blank) - nothing is in the way, the next boot formats it"
        )
    return stale


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--erase",
        action="store_true",
        help=f"erase 0x{ADDRESS:06X} + 0x{SIZE:X} bytes and read back that it worked",
    )
    args = parser.parse_args()

    print(
        f"reading 0x{HEAD:X} bytes at 0x{ADDRESS:06X} "
        f"(the chip must already be in download mode - see the usage above)...",
        flush=True,
    )
    data = read_region(ADDRESS, HEAD)
    stale = report(data, ADDRESS)

    if b"littlefs" in data[:BLOCK]:
        print(
            "  the superblock magic is there: this is a LittleFS, erasing it would lose the history"
        )
        if args.erase:
            print(
                "refusing to erase - delete the file with `history clear` instead if that is what you want"
            )
            return 2
    if not args.erase:
        if stale:
            print(
                f"\nnext step: python {Path(__file__).parent.name}/{Path(__file__).name} --erase"
                "   (only if the bytes above are stale)"
            )
        else:
            print(
                "\nthe region is already blank - an erase would change nothing, so look for the "
                "mount failure itself further up in the log"
            )
        return 0

    print(
        f"\nerasing 0x{SIZE:X} bytes at 0x{ADDRESS:06X} (the live nvs at 0x310000 stays untouched)...",
        flush=True,
    )
    esptool(
        "erase-region", hex(ADDRESS), hex(SIZE)
    )  # --after no-reset: stay in the bootloader
    print(
        "\nreading back the first block to confirm the erase "
        "(this last step also reboots the board)...",
        flush=True,
    )
    try:
        head = read_region(ADDRESS, BLOCK, after="hard-reset")
    except SystemExit:
        print(
            "  the chip did not answer for the read-back: the reset already worked and it is\n"
            "  running the app again (the erase ran before that) - so read the log instead:"
        )
        print(
            "    esphome logs config.yaml   # expect: 'is unformatted - formatting it (first boot)'"
        )
        return 0
    if all(byte == 0xFF for byte in head):
        print(
            "  verified: all 0xFF - reboot the board and the first boot formats it "
            "(`esphome logs config.yaml` shows `is unformatted - formatting it (first boot)` "
            "and then `mounted on /littlefs`)"
        )
        return 0
    print(
        "  the erased range did not read back as 0xFF - the flash is the suspect, not the table"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())

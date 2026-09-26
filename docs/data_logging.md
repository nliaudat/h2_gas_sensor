# Data logging - the board's own history

The board keeps a rolling history of its readings in a time-series database on a
LittleFS partition (`tsdb` component, [`../esphome/packages/tsdb.yaml`](../esphome/packages/tsdb.yaml)).
It exists for the case this project is built for: the battery room has no usable
network, Home Assistant is unreachable, and something happened. The live entities
are gone from the dashboard, but the last days of measurements are still on the
board.

> The history has exactly the accuracy of the live entities - see the caveats in
> [`mq8_sensor_guide.md`](mq8_sensor_guide.md) and
> [`mics5524_guide.md`](mics5524_guide.md). It is a second copy of the
> measurement, not a certified recording.

## What is stored

One row per write interval (`history_write_interval`, 60 s by default) with four
columns, all taken from the *last published value* of an existing entity:

| Column | Source entity | `scale` | Stored range |
|---|---|---|---|
| `h2_mq8_ppm` | `H2 (MQ-8)` | 1 | 0 - 10 000 ppm |
| `h2_trace_ppm` | `H2 trace (MiCS-5524)` | 1 | 0 - 1 000 ppm |
| `temperature` | ambient temperature (DHT22) | 10 | -3276.7 - 3276.7 °C |
| `humidity` | ambient humidity (DHT22) | 10 | -3276.7 - 3276.7 %RH |

The two ratios (`MQ-8 RS-R0 ratio`, `MiCS-5524 ratio`) are **not** in the file.
They stay live entities - the 1 Hz log and the Home Assistant recorder keep them -
but a record costs `4 + 2 * columns` bytes, so two fewer columns buy 22.3 days of
history out of the same `max_file_size` instead of 16.7 (see the capacity
section); the two ppm values plus the ambient pair are what a later reading of an
event needs.

Removing or adding a column is a **schema change**, not just a config change: the
file header stores the column count and esp_tsdb refuses a file whose count
differs from `columns:` (it would mislabel every value in it).
`recreate_on_schema_change: true` (set in the package) is what makes the update
survivable: the first boot of the new firmware deletes that file and starts an
empty one - the log says `history recreated (the column set changed)`, the old
history is gone. Without the option the component would stay failed, writing
nothing and taking the diagnostics and the history buttons down with it.

The engine stores `int16_t`, so a column is quantised: `scale: 10` keeps one
decimal (0.1 °C), `scale: 100` two (0.01 ratio). Decoding is

```
value = (raw - offset) / scale        raw = round(value * scale + offset)
```

A value outside the `int16_t` range is **clamped** to `-32767 .. 32767`
(`-32768` is the engine's "no value" marker and is never produced by a normal
reading). A row is only written when every column has a fresh value: a sensor
that never published, or that publishes `unknown`/NaN, drops the row
(`on_missing: skip`, the default) instead of storing a zero - a gap in the
history is honest, a zero would look like clean air.

The database file also carries the column names in its header (31 characters
each) and its own `.h0`/`.h1` header sidecars next to it in the same directory -
`/littlefs/history.tsdb`, `/littlefs/history.tsdb.h0`, `/littlefs/history.tsdb.h1`.

## Where it lives (and why the first flash needs a cable)

`partition_size: 512KB` adds one partition to the partition table:

```
# ESPHome generates this automatically (esp32.add_partition)
otadata, data, ota, , 0x2000,
phy_init, data, phy, , 0x1000,
app0, app, ota_0, , 0x180000,   # was 0x1C0000
app1, app, ota_1, , 0x180000,   # was 0x1C0000
nvs, data, nvs, , 0x70000,
littlefs, data, littlefs, , 0x80000,   # the history
```

The 512 KB is split between the two OTA slots, so each of them shrinks from
1.75 MB to 1.5 MB - the firmware (1 007 371 bytes with the history included) sits
at 64 % of its slot, which still leaves 565 493 bytes (about 552 KB) of headroom
for future releases. A
partition table change cannot be delivered by OTA: **flash once over USB**
(`esphome run config.yaml`) after enabling the package, then OTA works as before.

## Capacity and retention

The capacity follows from `max_file_size` (default `384KB`) and the number of
columns - one record is `4 + 2 * columns` bytes, a block holds 84 of them with 4
columns. The numbers below are asserted by
[`../esphome/tests/tsdb_math_test.cpp`](../esphome/tests/tsdb_math_test.cpp):

| Columns | Bytes/record | Records/block | `max_file_size` | Records | File at capacity | History at 60 s | History at 5 min |
|---|---|---|---|---|---|---|---|
| 4 (shipped) | 12 | 84 | 384 KB | 32 064 | 392 432 B (383 KB) | 22.3 days | 111.3 days |

`capacity_for_bytes()` is deliberately a little more conservative than the
engine's own `TSDB_CALC_MAX_RECORDS()` (which would allow 32 597 records): the
file really stays inside `max_file_size` instead of growing ~1.6 % past it.

The database is a **ring buffer**: once the capacity is reached the oldest row is
overwritten, so the file never grows beyond `max_file_size` and the history is
always "the newest N records". Halve the write interval and the history window
halves with it; raise `max_file_size` (and `partition_size`) if you want more.

`min_free_bytes` (default 32 KB) is the safety net: before the engine grows the
file it asks LittleFS how much space is left, and caps the capacity early if the
partition runs low - a copy-on-write filesystem needs free blocks even to
overwrite, and a full partition would break every *other* file on it too.

Other budgets, same four columns (measured by the same host test):

| `max_file_size` | Records | File at capacity | History at 60 s |
|---|---|---|---|
| 128 KB | 10 614 | 130 856 B | 7.4 days |
| 256 KB | 21 330 | 261 136 B | 14.8 days |
| 384 KB (shipped) | 32 064 | 392 432 B | 22.3 days |
| 768 KB | 64 262 | 786 328 B | 44.6 days |

A 1 MB budget is possible in principle (85 727 records) but needs a partition of
at least ~1.1 MB - and that would leave the two OTA slots below 1.2 MB, i.e. the
current firmware at more than 80 % of its slot. Not advisable without changing
the flash size.

A bigger `max_file_size` needs a bigger `partition_size`: the partition has to
hold the file plus `min_free_bytes`, and every KB of it is taken out of **both**
OTA slots. On this 4 MB flash, with the firmware the history package produces
(1 007 371 bytes):

| `partition_size` | Each OTA slot | Firmware share | Verdict |
|---|---|---|---|
| `256KB` | 1.625 MB | 59 % | fine, less history |
| `512KB` (shipped) | 1.5 MB | 64 % | the shipped configuration |
| `1MB` | 1.25 MB | 77 % | still usable, little headroom |
| `1.5MB` | 1.0 MB | 96 % | do not: no room left for the firmware to grow |

So `512KB`/`384KB` is the comfortable choice and `1MB`/`768KB` the practical
maximum on this 4 MB flash.

## Flash and RAM cost (measured baseline)

Measured on 2026-09-26 with ESPHome 2026.9.0 / ESP-IDF 5.5.5: `esphome compile
config.yaml` prints the summary, and `esp_idf_size --archives` / `--files` on
`esphome/.esphome/build/h2_sensor/build/h2_sensor.map` breaks it down. Compare a
future build against these numbers. The line of earlier runs of the same
configuration: 1 006 047 B of flash and 53 692 B of static DRAM (2026-09-25); the
CSV-dump/mount fixes and logging the two ratios cost +212 B / +8 B; selecting the
dump window by time instead of walking the history added +192 B of flash and no
RAM at all; dropping the two ratio columns again - a column is runtime config, it
costs no flash - and adding the `recreate_on_schema_change` recovery path came out
at 1 006 819 B (that build's `tsdb.cpp.obj` grew by 188 B). The current build is
1 007 371 B: the correction of the five review findings costs +552 B of flash -
the stored-schema probe (`read_stored_columns()` and `tsdbmath::stored_columns()`,
which reads the file's own header *before* anything is deleted), the dump's
count/skip pass that now aborts instead of printing old rows as the newest ones and
the one-shot widening of a short dump window - with no RAM or IRAM change at all:

| Memory | Used | Total | Free |
|---|---|---|---|
| Flash (image) | 1 007 371 B (64.0 %) | app slot 1 572 864 B | 565 493 B (36 %) |
| IRAM | 82 963 B (63.3 %) | 131 072 B | 48 109 B |
| DRAM (static) | 53 700 B (29.7 %) | 180 736 B | 127 036 B |

(The per-update `INFO` value line of the two gas components, `log_ppm:`, added
+176 B of flash and +16 B of static DRAM to the build that came before it.)

The history itself, object by object (`esp_idf_size --files`):

| Object | Flash |
|---|---|
| LittleFS - `lfs.c` 17 322, `esp_littlefs.c` 6 466, `littlefs_esp_part.c` 213 | 24 001 B |
| esp_tsdb - `tsdb_core` 5 363, `tsdb_query` 1 714, `tsdb_write` 1 511, `tsdb_buffer` 559, `tsdb_migrate` 195 | 9 342 B |
| `tsdb.cpp` (this component) | 5 849 B |
| VFS directory support (`require_vfs_dir()`, part of `vfs.c`) | ~500 B |
| the entities and automations of `packages/tsdb.yaml` | ~1 000 B |
| **total** | **~40 KB** |

Static RAM is 53 700 B (29.7 %) - `.bss` 36 456, `.data` 17 084 and 160 B of
`noinit`. Against the pre-history build that is +2 088 B (`.bss` +1 960, `.data`
+128), 8 B more than the 2026-09-25 measurement. A column is not a static at all:
the four columns are entries of the heap-allocated `columns_` vector, and the
component object itself is still 700 B of `.bss` - which is also why the four
columns (instead of six) and the one `bool` of `recreate_on_schema_change_` did
not move a single byte of this table. IRAM does not move either. While the
database is mounted roughly **6 - 7 KB of heap** are in use (4 KB buffer pool, the
LittleFS block caches, the esp_tsdb handle with its header copy and mutex);
`buffer_pool_size` and `min_free_bytes` are the knobs.

Reading the two size reports: before the history package the image was
904 299 B, now 1 007 371 B (+103 072 B, ~40 KB of it the feature's own code). The
remainder sits in objects of the base configuration that this change does not
touch - the current image carries `esp_timer_impl_lac.c.obj` with 91 777 B of
`.rodata` (the time/newlib data of the `time:`/SNTP support) and the Wi-Fi and
TLS data of the IDF components. An exact attribution needs an A/B build (comment
the `tsdb: !include` line, rebuild, diff `--archives`), because `esphome clean`
deletes the older map file.

Decision (2026-09-25): the shipped `512KB`/`384KB` stays as it is - the 565 493
bytes of the app slot that stay free (~1 - 2 KB per release of headroom), an
untouched IRAM and the 22.3 days of history are worth the 40 KB.

## Durability: what a power cut costs

LittleFS is a log-structured filesystem: a file only becomes visible (its
directory entry is published) when it is **closed**. A database file that is held
open and never closed would be *gone* after a reboot, not just truncated. The
component therefore calls `tsdb_sync_h()` (close + reopen, 30 - 80 ms) every
`sync_interval` - and once after the very first write, which is the commit that
makes the file survive at all.

| `sync_interval` | Data at risk on a power cut | Flash commits |
|---|---|---|
| `0s` | none (after every write) | 1 per write |
| `60s` (default, = write interval) | at most the newest row | 1 per write |
| `15min` | up to 15 min | 1 per 15 writes |

The board is rebooted on purpose (Monday 06:00, [`packages/time.yaml`](../esphome/packages/time.yaml))
and Home Assistant-less operation reboots it every `api: reboot_timeout`
(30 min): a clean shutdown calls `on_shutdown()`, which syncs and closes the
database properly. The table above is about the *unclean* case - a power cut.

The deadline is checked after **every** write interval, including the ones in
which the row was dropped (`on_missing: skip` left a column empty): a gap in the
history never postpones the commit of the rows that *are* stored, so "at most the
newest row" is the age of the newest written row, not of the newest attempt. An
interval in which nothing was written at all costs no flash commit - only a file
that changed since the last commit is synced.

If the sync itself fails (flash wear-out, filesystem corruption) the component
counts it in `... history write errors` and in the log; it does not silently
continue as if the data were safe.

## Flash wear

Every write is one metadata commit in LittleFS; the table below is the reason the
default is 60 s and not 5 s:

| Write interval | Commits per year | Notes |
|---|---|---|
| 1 min | 525 600 | LittleFS rotates its metadata blocks (`CONFIG_LITTLEFS_BLOCK_CYCLES`, default 512) and the data blocks are spread across the partition, so the wear per block stays far below the ~100 000 cycles of the flash |
| 5 min | 105 120 | 5× less wear, 5× less history resolution |
| 15 min | 35 040 | plenty for a slow drift; the pre-alarm band is covered by the live entities |

The engine writes its header to a small sidecar file (`.h0`/`.h1`) instead of
rewriting offset 0 of a large file - on LittleFS the latter costs time
proportional to the file size, which is what made writes of a multi-hundred-KB
database slow in earlier engine versions.

## Reading the data out

1. **Aggregates** - `... history H2 average 1h` (and the trace average) are
   published every `aggregate_interval` (5 min) as ordinary sensors: they appear
   in the Home Assistant history, need no extra network surface and cover a
   window longer than the (excluded) 1 Hz stream. Without a row in the window
   they are `unknown`, never `0`.
2. **CSV dump** - the `... history dump (log)` button prints the newest
   `dump_rows` (60) rows **that are stored** to the log. The window is the last
   `dump_rows` write intervals ending at the newest record, doubled as a margin
   for timestamp jitter and a single gap, so the engine can **seek** to it in
   O(log n) block reads: a press reads a bounded window instead of scanning the
   whole history, and no longer stalls the loop (or the 1 s readings) while it
   runs. Only a gap wider than the margin leaves that window with fewer rows than
   asked for - the dump then widens it once to the whole history, so the newest
   `dump_rows` *stored* rows are still what you get, and the log says that it
   widened (only a database that holds fewer than `dump_rows` rows prints fewer,
   and it says that too). Read them with
   `esphome logs config.yaml` over the network, or over USB; the first line names
   the columns, the values are already decoded:

   ```
   [tsdb] history.tsdb: csv-dump begin (newest 60 records, window [1790353800, 1790361000], timestamp,h2_mq8_ppm,... ...)
   [tsdb] history.tsdb: 1790361000,52.0000,7.0000,21.4000,45.1000
   [tsdb] history.tsdb: csv-dump end (60 rows, 61 older skipped)
   ```
3. **Diagnostics** - `... history records`, `... history used/free`,
   `... history oldest/newest`, `... history write errors` and the
   `... history logs` text sensor describe the store itself; `oldest`/`newest`
   are `timestamp` sensors, so the covered window is visible at a glance.
4. **`... history clear`** deletes every stored row. The calibration of the
   sensors lives elsewhere (NVS) and is not touched.

## What it does not do

* **No file download.** Stock ESPHome has no HTTP endpoint for arbitrary files;
  the CSV dump over the log is the export path that needs no Home Assistant.
  (Streaming CSV over HTTP on a second port is possible and is the planned next
  step - it is not implemented.)
* **No per-column retention or trimming**: the ring buffer evicts the oldest
  *row*, all columns with it.
* **16 columns maximum** - the V3 file format limit. A change of the column set
  is a schema change, not a config change: the file is recreated and the stored
  rows of the old schema are gone (see [What is stored](#what-is-stored)). The
  file's own header has to prove the different column count; an open failure for
  any other reason (heap, filesystem, flash) keeps the history.
* **No storage of the reason** for a value (calibration state, correction
  factor): the raw chain is in the live entities and their diagnostics.
* The history is **not** a replacement for the Home Assistant recorder: it is
  what to look at when the recorder did not see the event.

## Operating it

The health check is one line in Home Assistant: `... history newest` should be
within `write_interval + sync_interval` of now, and `... history records` should
grow by one per write interval until the ring buffer is full.

| Symptom | Cause and what to do |
|---|---|
| `no valid time yet - no record is written` in the log, `records` stays 0 | Wi-Fi/SNTP is down, so the clock is still at the boot counter. Expected offline: the history - and the outage coverage it is meant to provide - starts with the first SNTP sync after power-up, so a board that boots without a network writes nothing until then. Only for a permanently offline board: `require_time: false` (the timestamps then only order the rows, they are not real dates) or a local RTC. |
| `records` is 0 although the clock is valid | The database could not be opened - look for `esp_tsdb could not open` / `mounting 'littlefs' ... failed` earlier in the log. The component marks itself failed and stops writing rather than pretend. |
| `no data/littlefs partition labelled 'littlefs'` after an update | The partition table was not flashed - OTA does not move partitions. Flash once over USB. |
| `partition 'littlefs' is unformatted - formatting it (first boot)` | Normal on the first boot after adding the package (or after a full flash erase). The partition is only formatted when *every* byte is `0xFF`: a database that holds data but fails to mount (or whose first block happens to be erased) is never overwritten. The history starts empty. |
| `X has no value - record dropped (N dropped so far)` | That entity published `unknown`/NaN: a pending calibration, a heater warm-up or a broken sensor. Rows are missing for as long as it lasts - that is the intended `on_missing: skip` behaviour. |
| `capacity is capped early when ... drops below ... bytes` | The free-space guard fired: the partition is nearly full. Raise `partition_size` (and `max_file_size`), or let the ring buffer overwrite. |
| `write errors` above 0 | Writes or syncs failed (filesystem or flash trouble). Check `free`, and see [`troubleshooting.md`](troubleshooting.md). |
| `opening as 4 columns failed and the file stores 6` once after an update that changed `columns:` | the stored file was written with another column count - a schema change, which esp_tsdb refuses to open | expected once: `recreate_on_schema_change: true` (set in the package) deletes that file and starts an empty one, the next line is `history recreated (the column set changed)`. Without the option the component stays failed until the file is deleted (`history clear` deletes the rows but keeps the file). |
| `opening failed but the stored schema is ... - keeping the file` | the open failed for a reason other than a different column set (heap, filesystem, flash trouble), or the stored schema could not be read | nothing is deleted on purpose: history is only thrown away when the file's own header proves the column set changed. Look for the real cause earlier in the log. |
| `csv-dump window holds N of M rows (a gap) - widening it to the whole history` | A gap in the history (`on_missing: skip` wrote nothing during an outage) is wider than the dump window's 2x margin, so the window held fewer rows than asked for although older ones exist | expected and harmless: the dump widens its range **once** to the whole history and still prints the newest `dump_rows` *stored* rows. It only costs the block-by-block walk that the time-selected window exists to avoid, and only in that gap case. |
| `csv-dump count failed - not dumping ...` | the engine could not count the records of the dump window (filesystem/flash trouble) | the dump stops instead of printing old rows as the newest ones. Check `write errors` and `free`, see [`troubleshooting.md`](troubleshooting.md). |
| The history is empty after a firmware change | Adding or removing a column is a schema change: the old file is deleted and a new one starts (`recreate_on_schema_change`). A change of `update_interval`/`sync_interval` keeps the data; a change of `file:`, `partition_size` or a full flash erase also starts a new database. |

## See also

* [`../esphome/components/tsdb/README.md`](../esphome/components/tsdb/README.md) -
  every option of the component.
* [`home_assistant_alerts.md`](home_assistant_alerts.md) - the live entities and
  the automations; the history is the backup for the window they miss.
* [`offline_mode.md`](offline_mode.md) - why the board may be offline for days,
  and how to reach it in that case.
* [`troubleshooting.md`](troubleshooting.md) - the general symptoms.

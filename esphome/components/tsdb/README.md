# tsdb

ESPHome hub that keeps a **persistent history** of the board's own readings in a
time-series database on a LittleFS partition, so an event that happens while Home
Assistant (or the whole network) is unreachable is still there afterwards.

The storage engine is [`zakery292/esp_tsdb`](https://github.com/zakery292/esp_tsdb)
(MIT): a data-agnostic time-series database with columnar storage (one row of
`int16_t` columns per sample), a sparse time index, built-in aggregations and a
ring buffer that overwrites the oldest row once the capacity is reached. The
filesystem is [`joltwallet/littlefs`](https://github.com/joltwallet/esp_littlefs)
(MIT). Both are pulled from the ESP-IDF component registry, pinned by version,
and both are only *build* dependencies - this repository ships no third-party
code.

Role in this project: [`packages/tsdb.yaml`](../../packages/tsdb.yaml) logs the
four readings that explain a hydrogen event (`H2 (MQ-8)`, `H2 trace (MiCS-5524)`,
ambient temperature and humidity; the two RS/R0 ratios stay live entities of the
gas packages, they are just not stored - see
[`docs/data_logging.md`](../../../docs/data_logging.md) for the user-facing
description). The entities, the calibration and the alarm logic stay exactly as
they are - the component only reads the last published value of each configured
sensor.

> The history is a **local** copy of the measurement, not a substitute for it.
> The gas values in it have the same accuracy, drift and cross-sensitivity as the
> live entities, and the file is only as good as the flash it lives on.

## Minimal configuration

```yaml
tsdb:
  id: history
  time_id: my_time            # required while `require_time: true` (the default)
  columns:
    - sensor: mq8
      name: h2_mq8_ppm
```

That is a 512 KB partition (both OTA slots shrink by 256 KB),
`/littlefs/history.tsdb`, one row every 60 s and a `tsdb_sync_h()` after every
write. The complete, ready-to-use version is
[`packages/tsdb.yaml`](../../packages/tsdb.yaml).

## How a row is written

Every `update_interval` (the write interval):

1. the clock is checked - with `require_time: true` (default) nothing is written
   while the time is not valid yet (a record with a boot counter as its timestamp
   would be worse than a gap);
2. every column is read: `raw = round(value * scale + offset)`, clamped to the
   `int16_t` range `-32767 .. 32767` (`-32768` is reserved for "no value");
3. a column whose sensor has never published, or publishes `NaN`, follows
   `on_missing:` - `skip` (default) drops the whole row (a gap is honest, a zero
   is not), `hold` repeats the last value written for that column, `sentinel`
   writes `-32768` for it;
4. the row is appended (`tsdb_write_h`) and - at most every `sync_interval` -
   committed with `tsdb_sync_h()`.

Reading a column back is `value = (raw - offset) / scale`; the CSV dump of the
"history dump" button prints the engineering values already decoded.

## Options

| Key | Default | Description |
|---|---|---|
| `file` | `history.tsdb` | Database file name inside `mount_point`. The engine adds `.h0`/`.h1` sidecars next to it (its header slots - see below). |
| `mount_point` | `/littlefs` | VFS path the partition is mounted on. |
| `partition` | `littlefs` | Partition label; the component registers it with ESPHome (`esp32.add_partition()`), so it appears in `partitions.csv`. |
| `partition_size` | `512KB` | Size of that partition, 4 KB aligned. It is taken out of the two OTA slots (each shrinks by half of it). See the sizing table below. |
| `format_on_first_boot` | `true` | Format the partition when it is *blank* (never formatted). Blank means **every byte is `0xFF`** - the whole partition is scanned before formatting, because a partition that holds data but does not mount (or whose head happens to be erased) must **not** be reformatted: that would throw the history away. |
| `recreate_on_schema_change` | `false` | Delete a database the engine refuses to open **when the file's own header proves it was written with another column count** (see [Columns](#columns)) and start an empty one. An open failure with an unreadable or matching stored schema - heap, filesystem or flash trouble - keeps the file: only a proven schema change may throw history away. The old rows are lost; that is the point: without the option the component stays failed. Set to `true` where a column change should be deliverable by OTA alone. |
| `max_file_size` | `384KB` | Data budget of the database file. The capacity (`max_records`) is derived from it; the file never grows beyond it (ring buffer). |
| `index_stride` | `380` | Records between two entries of the sparse time index (esp_tsdb default). Smaller = faster lookups, more index bytes. |
| `buffer_pool_size` | `4KB` | Buffer pool for block I/O (esp_tsdb). Internal RAM on a WROOM board; raise it (with `memory: psram`) on a WROVER. |
| `memory` | `auto` | `auto`, `internal` or `psram` - where the buffer pool is allocated (`psram` requires a `psram:` component). |
| `paged_allocation` | `false` | Allocate the pool in pages instead of one contiguous block (helps on a fragmented heap). |
| `page_size` | `2KB` | Page size for `paged_allocation`. |
| `update_interval` | `60s` | Write interval - one row per interval. |
| `sync_interval` | `60s` | `tsdb_sync_h()` cadence. LittleFS only publishes a file on close, so this is **the age of the data a power cut can lose**. `0s` = after every write. |
| `min_free_bytes` | `32KB` | Free-space guard: the engine caps the capacity early (and switches to ring eviction) when the partition drops below this. `0` disables it. |
| `require_time` | `true` | Refuse to write while the clock is invalid (< 2001-09-09). Set `false` to accept wrong timestamps on a board without a time source: the rows then only *order* the readings, a boot-counter timestamp is not a date. A board that boots offline writes nothing until the first SNTP sync - the log says so once per boot. |
| `time_id` | – | The `time:` component the timestamps come from. Required while `require_time: true`. |
| `on_missing` | `skip` | `skip`, `hold` or `sentinel` (see above). `hold` needs a value it can repeat: a column that has never published is dropped even with `hold`. |
| `aggregate_window` | `1h` | Time window of the per-column aggregates. |
| `aggregate_interval` | `5min` | How often the aggregates are computed and published. `0s` disables them. |
| `columns` | – | 1..16 columns (see below). |
| `dump_rows` | `60` | Rows printed by the CSV dump: the newest `dump_rows` records that are *stored*. The window is the last `dump_rows` write intervals ending at the newest record (over-provisioned 2x), which the engine seeks to directly - a dump reads a bounded window (~2x `dump_rows` rows), never the whole history, so it cannot stall the loop. A gap wider than the margin makes the dump widen the window to the whole history once (logged), so the newest `dump_rows` *stored* rows are still printed; only a database holding fewer rows than `dump_rows` prints fewer. A failed count aborts the dump instead of printing old rows as the newest. |
| `records_sensor`, `used_sensor`, `free_sensor`, `oldest_sensor`, `newest_sensor`, `errors_sensor` | – | Optional diagnostic sensors: rows stored, bytes used/free on the partition, oldest/newest timestamp, write+sync errors. |
| `log_sensor` | – | Optional `text_sensor` that mirrors the important log lines (open, clear, flush, dropped rows, errors). |

## Columns

```yaml
  columns:
    - sensor: mq8                 # id of the source sensor (required)
      name: h2_mq8_ppm            # column name in the file header (required, <= 31 chars, unique)
      scale: 1                    # raw = round(value * scale + offset)
      offset: 0
      average_sensor: my_average  # optional: AVG/MIN/MAX over `aggregate_window`
      min_sensor: my_min
      max_sensor: my_max
```

* The engine stores `int16_t`, so pick a `scale` that keeps the expected range
  inside `-32767 .. 32767`: ppm and dBm are `1`, °C and %RH are `10`,
  `RS/R0` of the MQ-8 (up to ~70 in clean air) is `100`, the MiCS-5524 ratio
  (~1.0) is `1000`, volts are `1000` (mV). A value outside the range is clamped,
  not wrapped - and never silently turned into `-32768`.
* With `on_missing: hold` the last *written* value of a column is repeated; a
  column that has not published since boot has nothing to repeat and drops the row
  exactly like `skip`.
* At most **16** columns: that is the base-parameter limit of the V3 file format.
  The column count is part of the file schema: esp_tsdb refuses to open a file
  that was written with another count, so adding or removing a column makes the
  component delete that file and start a new one
  (`recreate_on_schema_change: true`; the old rows are lost). It deletes on that
  proof only: the count is read from the file's own header (magic `ETSD` at
  offset 0, the count at offset 8), so an open failure for any other reason
  (heap, filesystem, I/O) keeps the file for the next boot.
  `tsdb_migrate_schema_h()` could convert it in place instead - deliberately not
  implemented here.
* `average_sensor`, `min_sensor` and `max_sensor` are ordinary `sensor:`s (e.g.
  `platform: template`) that the component publishes to; with no row in
  `aggregate_window` they get `unknown` (never a `0`).

## Functions and buttons

| Call | What it does |
|---|---|
| `id(history).request_flush()` | Commit immediately (`tsdb_sync_h()`) - for "I am about to cut the power". |
| `id(history).request_dump()` | Print the newest `dump_rows` *stored* rows as CSV to the log (header line with the column names, then one line per row, engineering values decoded). The window is the last `dump_rows` write intervals, so the engine seeks to it instead of scanning the history; a gap wider than the 2x margin widens it once to the whole history (the log says so), so the request still returns the newest `dump_rows` stored rows. A failed count aborts the dump rather than print old rows as the newest. |
| `id(history).request_clear()` | Delete every stored row (`tsdb_clear_h()`); the file, the partition and the calibration stay. |
| `id(history).is_ready()` | `true` once the partition is mounted and the database is open. |
| `id(history).get_records()` | Rows currently stored (0 until the first statistics pass). |

## Partition sizing

`partition_size` is split between the two OTA slots by ESPHome
(`esp32.add_partition()`); the *capacity* of the database comes from
`max_file_size`, so pick a partition that comfortably holds it plus
`min_free_bytes`:

| `partition_size` | Each OTA slot | Firmware share | Reasonable `max_file_size` |
|---|---|---|---|
| `256KB` | 1.625 MB | 59 % | 192 KB |
| `512KB` (default) | 1.5 MB | 64 % | 384 KB |
| `1MB` | 1.25 MB | 77 % | 768 KB |

All of them are recorded in `partitions.csv` as one `data, littlefs` partition
next to the existing `nvs` partition; nothing else moves but the two app slots.
A partition table change can only be delivered by a **USB flash**, so the first
run after adding the package needs a cable. The measured flash/RAM cost of the
whole feature (per object, and the numbers to compare a future build against) is
in [`docs/data_logging.md`](../../../docs/data_logging.md#flash-and-ram-cost-measured-baseline):
~40 KB of flash, +2 KB of static RAM, no IRAM.

## Durability and flash wear

LittleFS publishes a file only when it is closed, so `sync_interval` is exactly
the maximum amount of data a power cut can lose: the component calls
`tsdb_sync_h()` (close + reopen) on that cadence, and once after the first write
(that is the commit which makes the database survive a reboot at all). A clean
restart (the Monday reboot, OTA, `safe_mode:`) syncs and closes through
`on_shutdown()`. The deadline is evaluated after every write interval - including
the ones that dropped a row - so the last record that *was* written is never left
unsynced for longer than `sync_interval` while the history has a gap; an interval
in which nothing was written costs no commit at all. Every write costs one
LittleFS metadata commit, which is why the default is 60 s rather than 5 s; the
numbers are in [`docs/data_logging.md`](../../../docs/data_logging.md).

## Limits

* ESP32 + the ESP-IDF framework only (`esp32: framework: type: esp-idf`), and it
  needs a dedicated flash partition - see `docs/data_logging.md`.
* No file download: the CSV dump over the log is the export path that needs no
  Home Assistant. Streaming CSV over HTTP is the planned next step.
* The ring buffer evicts whole rows, so all columns share one retention.
* `-32768` is the "no value" marker of the engine: a column value of -32768 is
  never written by a normal reading (`encode()` clamps to `-32767`).

## Implementation notes

| File | Content |
|---|---|
| `tsdb_math.h` | The pure parts (int16 encoding, file geometry, capacity and retention arithmetic); no ESPHome/ESP-IDF include, so it runs in the host test. |
| `tsdb.h` / `tsdb.cpp` | The component: partition and filesystem, `tsdb_open()`, the write/sync cadence, aggregates, CSV dump, diagnostics. |
| `__init__.py` | Config schema plus the two registry dependencies, the partition request, `require_vfs_dir()` and the LittleFS sdkconfig option. |
| `../../tests/tsdb_math_test.cpp` | Host test of the header above, and the numbers quoted in the docs. |

`tsdb.cpp` static-asserts the geometry constants against the real esp_tsdb
macros/structs (`TSDB_BLOCK_SIZE`, `TSDB_BLOCK_HEADER_SIZE`,
`TSDB_CALC_MAX_RECORDS()`, `sizeof(tsdb_header_t)`, `tsdb_alloc_strategy_t`), so
an engine upgrade that changes the file format fails the build instead of
silently invalidating `docs/data_logging.md`.

## Credits and licence

* [zakery292/esp_tsdb](https://github.com/zakery292/esp_tsdb) 2.4.3 - the
  time-series engine (columnar blocks, sparse index, ring buffer, aggregations,
  sidecar header). MIT.
* [joltwallet/esp_littlefs](https://github.com/joltwallet/esp_littlefs) 1.22.3 -
  the LittleFS VFS component. MIT, and LittleFS itself is
  [BSD-3-Clause](https://github.com/littlefs-project/littlefs).
* Both are fetched at build time from the ESP-IDF component registry; this
  component is the glue and adds no third-party code to the repository.

#pragma once
//
// Pure math of the tsdb data logger: the int16_t encoding of a sensor reading,
// the file geometry of the esp_tsdb engine and the retention it buys.
//
// Geometry mirrored from esp_tsdb 2.4.3 (MIT, https://github.com/zakery292/esp_tsdb),
// taken from include/esp_tsdb.h and src/tsdb_internal.h:
//
//   sizeof(tsdb_header_t)                         = 584 bytes (packed struct; the
//                                                   "512" in the upstream README is stale)
//   TSDB_BLOCK_SIZE                               = 1024 bytes, 8 byte block header
//   record                                        = 4 byte timestamp + 2 bytes per column
//   index                                         = 8 bytes per index_stride records
//   TSDB_CALC_MAX_RECORDS(bytes, columns)         = (bytes - 2048) / (4 + 2 * columns)
//
// `tsdb.cpp` static_asserts these helpers against the real macros of the engine,
// so a drift in esp_tsdb breaks the build instead of the documentation.
//
// This header is intentionally free of ESPHome / ESP-IDF dependencies so the very
// same code runs in the host test (see esphome/tests/tsdb_math_test.cpp).

#include <cmath>
#include <cstdint>
#include <limits>

namespace esphome::tsdb::tsdbmath {

/// Highest value the engine stores per column (int16_t).
static constexpr int16_t RAW_MAX = 32767;
/// Lowest int16_t, reserved as the "no value" marker written by `on_missing: sentinel`.
static constexpr int16_t RAW_UNKNOWN = -32768;
/// A reading is clamped here first, so a saturated value is never mistaken for
/// RAW_UNKNOWN.
static constexpr int16_t RAW_MIN = -32767;

/// How a column without a fresh value is handled (mirrors the Python mapping).
enum MissingPolicy : uint8_t {
  MISSING_SKIP = 0,      ///< drop the whole record
  MISSING_HOLD = 1,      ///< repeat the last value written for that column
  MISSING_SENTINEL = 2,  ///< write RAW_UNKNOWN for that column
};

/// Buffer pool location (mirrors tsdb_alloc_strategy_t).
enum MemoryMode : uint8_t {
  MEMORY_INTERNAL = 0,  ///< TSDB_ALLOC_INTERNAL_RAM
  MEMORY_PSRAM = 1,     ///< TSDB_ALLOC_PSRAM
  MEMORY_AUTO = 2,      ///< TSDB_ALLOC_AUTO
};

/// esp_tsdb file geometry (esp_tsdb 2.4.3).
static constexpr uint32_t BLOCK_SIZE = 1024;
static constexpr uint32_t BLOCK_HEADER_SIZE = 8;
static constexpr uint32_t HEADER_BYTES = 584;  ///< sizeof(tsdb_header_t)
static constexpr uint32_t TIMESTAMP_BYTES = 4;
static constexpr uint32_t COLUMN_BYTES = 2;
static constexpr uint32_t INDEX_ENTRY_BYTES = 8;
static constexpr uint32_t INDEX_DEFAULT_STRIDE = 380;
/// What TSDB_CALC_MAX_RECORDS() keeps for the header and the sparse index.
static constexpr uint32_t MAX_RECORDS_RESERVE = 2048;

/// Timestamps below this are a boot counter, not a clock: 2001-09-09T01:46:40Z.
static constexpr uint32_t MIN_VALID_TIMESTAMP = 1000000000;

/// A reading that cannot be logged (NaN, infinity).
inline bool is_missing(float value) { return !std::isfinite(value); }

/// Whether a timestamp comes from a real clock (see MIN_VALID_TIMESTAMP).
constexpr bool is_valid_timestamp(uint32_t timestamp) { return timestamp >= MIN_VALID_TIMESTAMP; }

/// Bytes one record occupies in a block, for `columns` columns.
constexpr uint32_t record_bytes(uint32_t columns) { return TIMESTAMP_BYTES + COLUMN_BYTES * columns; }

/// Records that fit in one 1 KB block, for `columns` columns.
constexpr uint32_t records_per_block(uint32_t columns) {
  const uint32_t record = record_bytes(columns);
  if (record == 0)
    return 0;
  return (BLOCK_SIZE - BLOCK_HEADER_SIZE) / record;
}

/// Records that fit into `bytes` of database file - the arithmetic of the
/// upstream TSDB_CALC_MAX_RECORDS(bytes, columns) macro.
constexpr uint32_t max_records_for_bytes(uint64_t bytes, uint32_t columns) {
  const uint32_t record = record_bytes(columns);
  if (record == 0 || bytes <= MAX_RECORDS_RESERVE)
    return 0;
  return static_cast<uint32_t>((bytes - MAX_RECORDS_RESERVE) / record);
}

/// Size the data file needs for `records` records: the 584-byte header plus
/// whole 1 KB blocks plus the sparse index. It is an upper bound (the last
/// block counts as full) and the source of the numbers quoted in
/// docs/data_logging.md.
constexpr uint64_t file_bytes_for(uint32_t records, uint32_t columns, uint32_t index_stride = INDEX_DEFAULT_STRIDE) {
  if (records == 0 || columns == 0 || index_stride == 0)
    return 0;
  const uint32_t per_block = records_per_block(columns);
  if (per_block == 0)
    return 0;
  const uint64_t blocks = (records + per_block - 1) / per_block;
  const uint64_t index_entries = (records + index_stride - 1) / index_stride;
  return HEADER_BYTES + blocks * BLOCK_SIZE + index_entries * INDEX_ENTRY_BYTES;
}

/// Largest number of records whose file still fits into `bytes`.
///
/// TSDB_CALC_MAX_RECORDS() ignores that a block only holds
/// `(1024 - 8) / record_size` records (63 of the 1008 bytes a 6-column block
/// uses), so its capacity overflows the budget by ~1.5 % - the solver here
/// shrinks the estimate until `file_bytes_for()` really fits.
constexpr uint32_t capacity_for_bytes(uint64_t bytes, uint32_t columns, uint32_t index_stride = INDEX_DEFAULT_STRIDE) {
  uint32_t records = max_records_for_bytes(bytes, columns);
  while (records > 0 && file_bytes_for(records, columns, index_stride) > bytes) {
    const uint64_t over = file_bytes_for(records, columns, index_stride) - bytes;
    const uint32_t step = static_cast<uint32_t>(over / record_bytes(columns)) + 1;
    records = step >= records ? 0 : records - step;
  }
  return records;
}

/// Seconds of history `records` records cover at `interval_seconds` each.
constexpr uint64_t history_seconds(uint32_t records, uint32_t interval_seconds) {
  return static_cast<uint64_t>(records) * interval_seconds;
}

/// Rows the CSV dump has to walk past so that the **newest** `rows` stored
/// records are the ones printed.
///
/// The engine's query iterates the ring buffer oldest first, so a dump of the
/// last `rows` records consumes `total_records - rows` earlier ones. `total_records`
/// is the *stored* row count of `tsdb_get_stats_h()` (never the capacity), which
/// is why the window is selected by count instead of by an estimated
/// `rows * write_interval` span: gaps in the history (`on_missing: skip`, a
/// changed write interval) then skip nothing extra, they only move the start.
constexpr uint32_t dump_skip(uint32_t total_records, uint32_t rows) {
  return total_records > rows ? total_records - rows : 0;
}

/// Encode an engineering value as the raw int16_t the engine stores:
/// `raw = round(value * scale + offset)`, clamped to the int16_t range.
/// A value that cannot be encoded at all (NaN, infinite, `scale == 0`) is
/// reported as RAW_UNKNOWN and must be filtered by the caller beforehand when
/// `raw == RAW_UNKNOWN` is reserved for "no value".
inline int16_t encode(double value, double scale, double offset = 0.0) {
  if (!std::isfinite(value) || scale == 0.0)
    return RAW_UNKNOWN;
  const double raw = std::round(value * scale + offset);
  if (raw < static_cast<double>(RAW_MIN))
    return RAW_MIN;
  if (raw > static_cast<double>(RAW_MAX))
    return RAW_MAX;
  return static_cast<int16_t>(raw);
}

/// Decode an aggregation result. AVG/MIN/MAX keep the int16_t domain of the
/// column; SUM and COUNT do not, so only the three are exposed as sensors.
inline double decode_aggregate(int32_t raw, double scale, double offset = 0.0) {
  if (raw == RAW_UNKNOWN || scale == 0.0)
    return std::numeric_limits<double>::quiet_NaN();
  return (static_cast<double>(raw) - offset) / scale;
}

/// Decode a raw column value back into engineering units:
/// `value = (raw - offset) / scale`. Returns NaN for the RAW_UNKNOWN marker.
inline double decode(int16_t raw, double scale, double offset = 0.0) {
  if (raw == RAW_UNKNOWN || scale == 0.0)
    return std::numeric_limits<double>::quiet_NaN();
  return (static_cast<double>(raw) - offset) / scale;
}

}  // namespace esphome::tsdb::tsdbmath

// Host side validation of components/tsdb/tsdb_math.h
//
// Build & run (no ESPHome/ESP-IDF needed):
//     g++ -std=c++17 -O2 -Wall -Wextra -I ../components/tsdb tsdb_math_test.cpp -o tsdb_math_test
//     ./tsdb_math_test
//
// The geometry numbers asserted here are the ones quoted in
// docs/data_logging.md and in the component README: the int16_t encoding of a
// column, the bytes per record, records per block, the capacity of a
// `max_file_size` and the history that capacity buys at a given write interval.
// The shipped configuration (packages/tsdb.yaml) has four columns, so the
// "shipped" numbers below are the 4-column ones.

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "tsdb_math.h"

using namespace esphome::tsdb::tsdbmath;

static int failures = 0;

static void check_close(const char *name, double actual, double expected, double eps = 1e-9) {
  const double scale = std::max(std::fabs(actual), std::fabs(expected));
  const bool ok =
      std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual - expected) <= eps + 1e-9 * scale;
  std::printf("%-62s %-4s got=%.10g expected=%.10g\n", name, ok ? "OK" : "FAIL", actual, expected);
  if (!ok)
    failures++;
}

static void check_true(const char *name, bool ok) {
  std::printf("%-62s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
    failures++;
}

static void check_nan(const char *name, double value) {
  const bool ok = std::isnan(value);
  std::printf("%-62s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
    failures++;
}

int main() {
  std::printf("== int16 encoding (raw = round(value * scale + offset)) ==\n");
  check_close("encode(4821 ppm, scale 1)", encode(4821.0, 1.0), 4821);
  check_close("encode(21.4 degC, scale 10)", encode(21.4, 10.0), 214);
  check_close("encode(-40 degC, scale 10)", encode(-40.0, 10.0), -400);
  check_close("encode(70 RS/R0, scale 100)", encode(70.0, 100.0), 7000);
  check_close("encode(1.2345 ratio, scale 1000)", encode(1.2345, 1000.0), 1235);
  check_close("encode(3.300 V, scale 1000)", encode(3.3, 1000.0), 3300);
  check_close("encode(4821 ppm, scale 1, offset -100)", encode(4821.0, 1.0, -100.0), 4721);
  check_close("encode(32767, scale 1, offset 100) saturates", encode(32767.0, 1.0, 100.0), RAW_MAX);
  check_close("encode(-32700, scale 1, offset -100) saturates", encode(-32700.0, 1.0, -100.0), RAW_MIN);
  check_close("encode(1e6, scale 1) saturates at RAW_MAX", encode(1e6, 1.0), RAW_MAX);
  check_close("encode(NaN) is RAW_UNKNOWN", encode(NAN, 1.0), RAW_UNKNOWN);
  check_close("encode(INFINITY) is RAW_UNKNOWN", encode(INFINITY, 1.0), RAW_UNKNOWN);
  check_close("encode(1.0, scale 0) is RAW_UNKNOWN", encode(1.0, 0.0), RAW_UNKNOWN);

  std::printf("\n== decoding ==\n");
  check_close("decode(4821, scale 1)", decode(4821, 1.0), 4821.0);
  check_close("decode(214, scale 10)", decode(214, 10.0), 21.4);
  check_close("decode(7000, scale 100)", decode(7000, 100.0), 70.0);
  check_close("decode(encode(21.43, scale 10), scale 10) = 21.4", decode(encode(21.43, 10.0), 10.0), 21.4);
  check_nan("decode(RAW_UNKNOWN) is NaN", decode(RAW_UNKNOWN, 1.0));
  check_nan("decode(1, scale 0) is NaN", decode(1, 0.0));
  check_close("decode_aggregate(4821, scale 1)", decode_aggregate(4821, 1.0), 4821.0);
  check_close("decode_aggregate(7000, scale 100)", decode_aggregate(7000, 100.0), 70.0);
  check_nan("decode_aggregate(RAW_UNKNOWN) is NaN", decode_aggregate(RAW_UNKNOWN, 1.0));

  std::printf("\n== file geometry (esp_tsdb 2.4.3) ==\n");
  check_close("record_bytes(4 columns) = 4 + 4*2 (shipped)", record_bytes(4), 12);
  check_close("records_per_block(4) = (1024-8)/12 (shipped)", records_per_block(4), 84);
  check_close("record_bytes(6 columns) = 4 + 6*2", record_bytes(6), 16);
  check_close("records_per_block(6) = (1024-8)/16", records_per_block(6), 63);
  check_close("max_records_for_bytes(384KB, 4)", max_records_for_bytes(384 * 1024, 4), 32597);
  check_close("file_bytes_for(32597, 4) = header + blocks + index", file_bytes_for(32597, 4), 399608);

  std::printf("\n== retention ==\n");
  check_close("history_seconds(32597, 60 s)", history_seconds(32597, 60), 1955820);
  check_close("history_seconds(32597, 60 s) in days", history_seconds(32597, 60) / 86400.0, 22.6377, 1e-3);
  check_close("history_seconds(32597, 300 s) in days", history_seconds(32597, 300) / 86400.0, 113.1840, 1e-3);

  std::printf("\n== planning: capacity_for_bytes() keeps the file inside max_file_size ==\n");
  for (const uint32_t size_kb : {128u, 256u, 384u, 512u, 768u, 1024u}) {
    for (const uint32_t columns : {4u, 6u, 10u, 16u}) {
      const uint64_t budget = static_cast<uint64_t>(size_kb) * 1024;
      const uint32_t capacity = capacity_for_bytes(budget, columns);
      const uint32_t optimistic = max_records_for_bytes(budget, columns);
      const uint64_t bytes = file_bytes_for(capacity, columns);
      std::printf("   %uKB, %2u columns: %u records, %llu bytes (esp_tsdb macro says %u)\n", size_kb, columns, capacity,
                  static_cast<unsigned long long>(bytes), optimistic);
      char name[96];
      std::snprintf(name, sizeof(name), "   file_bytes_for(capacity(%uKB, %u columns)) <= %uKB", size_kb, columns,
                    size_kb);
      check_true(name, capacity > 0 && bytes <= budget);
      std::snprintf(name, sizeof(name), "   capacity(%uKB, %u columns) <= the optimistic macro", size_kb, columns);
      check_true(name, capacity <= optimistic);
    }
  }
  check_close("capacity_for_bytes(384KB, 4)", capacity_for_bytes(384 * 1024, 4), 32064);
  check_close("shipped: file at capacity is 392432 bytes", file_bytes_for(32064, 4), 392432);
  check_close("capacity_for_bytes(384KB, 4) in days at 60 s",
              history_seconds(capacity_for_bytes(384 * 1024, 4), 60) / 86400.0, 22.2667, 1e-3);
  check_close("capacity_for_bytes(384KB, 4) in days at 300 s",
              history_seconds(capacity_for_bytes(384 * 1024, 4), 300) / 86400.0, 111.3333, 1e-3);

  // The four budget rows of the table in docs/data_logging.md (four columns, the
  // shipped configuration).
  check_close("128KB budget -> 10614 records", capacity_for_bytes(128 * 1024, 4), 10614);
  check_close("128KB budget -> 130856 bytes", file_bytes_for(10614, 4), 130856);
  check_close("128KB budget -> 7.4 days at 60 s", history_seconds(10614, 60) / 86400.0, 7.3708, 1e-3);
  check_close("256KB budget -> 21330 records", capacity_for_bytes(256 * 1024, 4), 21330);
  check_close("256KB budget -> 261136 bytes", file_bytes_for(21330, 4), 261136);
  check_close("256KB budget -> 14.8 days at 60 s", history_seconds(21330, 60) / 86400.0, 14.8125, 1e-3);
  check_close("768KB budget -> 64262 records", capacity_for_bytes(768 * 1024, 4), 64262);
  check_close("768KB budget -> 786328 bytes", file_bytes_for(64262, 4), 786328);
  check_close("768KB budget -> 44.6 days at 60 s", history_seconds(64262, 60) / 86400.0, 44.6264, 1e-3);
  check_close("1MB budget -> 85727 records", capacity_for_bytes(1024 * 1024, 4), 85727);
  check_close("1MB budget -> 1047896 bytes", file_bytes_for(85727, 4), 1047896);
  check_close("1MB budget -> 59.5 days at 60 s", history_seconds(85727, 60) / 86400.0, 59.5326, 1e-3);

  std::printf("\n== timestamps and missing values ==\n");
  check_true("is_valid_timestamp(0) is false", !is_valid_timestamp(0));
  check_true("is_valid_timestamp(2026-09-25) is true", is_valid_timestamp(1790000000));
  check_true("is_valid_timestamp(2001-09-09) is true", is_valid_timestamp(MIN_VALID_TIMESTAMP));
  check_true("is_valid_timestamp(uptime 3600 s) is false", !is_valid_timestamp(3600));
  check_true("is_missing(NAN) is true", is_missing(NAN));
  check_true("is_missing(0.0f) is false (a real 0 ppm is a reading)", !is_missing(0.0f));

  std::printf("\n== CSV dump window ==\n");
  // The dump picks its window by *time* so the engine's query can seek to it in
  // O(log n) block reads (`tsdb_seek_start()` only runs while the start is later
  // than the oldest record); a count-based window that began at the oldest record
  // made every dump walk - and discard - the whole history first.
  const uint32_t now = 1790361000;
  const uint32_t long_history = 1789000000;  // far enough back that it never clamps
  check_close("dump_window_start(now, old, 60, 60 s, margin 1) is 1 h back",
              dump_window_start(now, long_history, 60, 60, 1), now - 3600);
  check_close("dump_window_start(..., margin 2) doubles the span", dump_window_start(now, long_history, 60, 60, 2),
              now - 7200);
  check_close("dump_window_start(...) is clamped to the oldest record", dump_window_start(now, now - 1000, 60, 60, 2),
              now - 1000);
  check_close("dump_window_start(...) never underflows below 1", dump_window_start(1000, 0, 60, 60, 2), 1);
  check_close("dump_window_start(...) stays clamped for a huge rows*days span",
              dump_window_start(now, 1700000000, 1000000, 86400, 4), 1700000000);
  check_true("dump_window_start(...) keeps the seek active for a young database",
             dump_window_start(now, long_history, 60, 60, 2) > long_history);
  // The margin over-provisions the window, so it can hold a few records more than
  // asked for; without the skip the dump would print the *oldest* of those.
  check_close("dump_skip(63, 60) skips the 3 oldest of the window", dump_skip(63, 60), 3);
  check_close("dump_skip(61, 60) skips 1 (the off-by-one case)", dump_skip(61, 60), 1);
  check_close("dump_skip(60, 60) skips nothing", dump_skip(60, 60), 0);
  check_close("dump_skip(37, 60) skips nothing (window holds fewer rows)", dump_skip(37, 60), 0);
  check_close("dump_skip(0, 60) skips nothing", dump_skip(0, 60), 0);
  check_true("dump_skip + rows == the window when the window is full", dump_skip(63, 60) + 60 == 63);

  std::printf("\n== configuration codes shared with __init__.py ==\n");
  check_close("MISSING_SKIP is 0", MISSING_SKIP, 0);
  check_close("MISSING_HOLD is 1", MISSING_HOLD, 1);
  check_close("MISSING_SENTINEL is 2", MISSING_SENTINEL, 2);
  check_close("MEMORY_INTERNAL is 0 (TSDB_ALLOC_INTERNAL_RAM)", MEMORY_INTERNAL, 0);
  check_close("MEMORY_PSRAM is 1 (TSDB_ALLOC_PSRAM)", MEMORY_PSRAM, 1);
  check_close("MEMORY_AUTO is 2 (TSDB_ALLOC_AUTO)", MEMORY_AUTO, 2);
  check_close("RAW_UNKNOWN is the int16 minimum", RAW_UNKNOWN, -32768);
  check_close("RAW_MIN leaves RAW_UNKNOWN free", RAW_MIN, -32767);
  check_close("RAW_MAX is the int16 maximum", RAW_MAX, 32767);
  check_close("INDEX_DEFAULT_STRIDE matches esp_tsdb", INDEX_DEFAULT_STRIDE, 380);
  check_close("MAX_RECORDS_RESERVE matches TSDB_CALC_MAX_RECORDS", MAX_RECORDS_RESERVE, 2048);

  if (failures == 0) {
    std::printf("\nAll tsdb_math tests passed.\n");
    return 0;
  }
  std::printf("\n%d tsdb_math test(s) FAILED.\n", failures);
  return 1;
}

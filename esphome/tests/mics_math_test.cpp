// Host side validation of the MiCS-5524 math, against the vendored copy of the
// header the build compiles (tests/mics_math.h, see the banner there).
//
// Build & run (no ESPHome/ESP-IDF needed):
//     g++ -std=c++17 -O2 -Wall -Wextra -I . mics_math_test.cpp -o mics_math_test
//     ./mics_math_test
//
// The expected values are recomputed here from the published sources, so the
// test doubles as a check that the ported formulas still match them:
//   * vendor model  -> DFRobot_MICS (MIT), src/DFRobot_MICS.cpp getGasData()
//   * datasheet model -> CO a=6.3 / b=-1.1, the two-point fit of the datasheet
//     curve quoted in the Home Assistant community thread (see
//     docs/mics5524_conversion.md)
//
// The upstream header reports every unusable input as NaN (a disconnected or
// over-range output, a non-positive reference), so the guards are asserted too.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "mics_math.h"

using namespace esphome::mics_5524::micsmath;

static int failures = 0;

static void check_close(const char *name, double actual, double expected, double eps = 1e-4) {
  const double scale = std::max(std::fabs(actual), std::fabs(expected));
  const bool ok = std::isfinite(actual) && std::isfinite(expected) &&
                  std::fabs(actual - expected) <= eps + 1e-6 * scale;
  std::printf("%-58s %-14s got=%.6g expected=%.6g\n", name, ok ? "OK" : "FAIL", actual, expected);
  if (!ok)
    failures++;
}

static void check_true(const char *name, bool ok) {
  std::printf("%-58s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
    failures++;
}

static void check_nan(const char *name, double value) {
  const bool ok = std::isnan(value);
  std::printf("%-58s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
    failures++;
}

static void check_inf(const char *name, double value) {
  const bool ok = std::isinf(value);
  std::printf("%-58s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
    failures++;
}

// Vendor table (DFRobot_MICS/src/DFRobot_MICS.cpp, getGasData()):
//   gas        threshold  gain        min    max
//   CO         0.425      0.000405    1      1000
//   CH4        0.786      0.000023    1000   25000
//   C2H5OH     0.306      0.00057     10     500
//   H2         0.279      0.00026     1      1000
//   NH3        0.8        0.0015      10     500
static const VendorCurve H2{0.279f, 0.00026f, 1.0f, 1000.0f};
static const VendorCurve CO{0.425f, 0.000405f, 1.0f, 1000.0f};
static const VendorCurve NH3{0.8f, 0.0015f, 10.0f, 500.0f};
static const VendorCurve CH4{0.786f, 0.000023f, 1000.0f, 25000.0f};

int main() {
  std::printf("== Vendor model: x = VCC - V_AO, ratio = x / x_air ==\n");
  check_close("air_value_from_voltage(3.0 V, VCC 5 V)", air_value_from_voltage(3.0f, 5.0f), 2.0);
  check_close("air_value_from_voltage(VCC) is 0", air_value_from_voltage(5.0f, 5.0f), 0.0);
  check_nan("air_value_from_voltage(above VCC) is NaN", air_value_from_voltage(5.5f, 5.0f));
  check_nan("air_value_from_voltage(NAN) is NaN", air_value_from_voltage(NAN, 5.0f));
  // The dead-input guard is VENDOR_DEAD_INPUT_FRACTION of VCC: 0.25 V at 5 V.
  check_close("VENDOR_DEAD_INPUT_FRACTION is 0.05", VENDOR_DEAD_INPUT_FRACTION, 0.05, 1e-6);
  check_nan("air_value_from_voltage(at the dead input) is NaN", air_value_from_voltage(0.25f, 5.0f));
  check_close("air_value_from_voltage(just above it)", air_value_from_voltage(0.26f, 5.0f), 4.74);
  check_close("ratio_from_air_value(2, 4)", ratio_from_air_value(2.0f, 4.0f), 0.5);
  check_nan("ratio_from_air_value(x, 0) is NaN", ratio_from_air_value(2.0f, 0.0f));
  check_nan("ratio_from_air_value(negative x, 4) is NaN", ratio_from_air_value(-1.0f, 4.0f));
  check_nan("ratio_from_air_value(NAN, 4) is NaN", ratio_from_air_value(NAN, 4.0f));

  std::printf("\n== Vendor model: H2 (threshold 0.279, gain 0.00026, 1..1000 ppm) ==\n");
  check_close("clean air (ratio 1.0) is 'not detected' -> 0 ppm", ppm_from_vendor_curve(1.0f, H2), 0.0);
  check_close("ratio at the threshold -> 0 ppm", ppm_from_vendor_curve(0.279f, H2), 0.0);
  // Note: a reading within one vendor gain step of the minimum rounds to 0 ppm
  // (the vendor clamps everything below min_ppm), so exact checks use multiples
  // of the gain that are unambiguous in float arithmetic.
  check_close("two gain steps below the threshold -> 2 ppm", ppm_from_vendor_curve(0.279f - 2.0f * 0.00026f, H2),
              2.0, 1e-3);
  check_close("just inside the 1 ppm limit -> ~1.05 ppm", ppm_from_vendor_curve(0.279f - 1.05f * 0.00026f, H2),
              1.05, 1e-3);
  check_close("ratio 0 (saturated sensor) -> the 1000 ppm top", ppm_from_vendor_curve(0.0f, H2), 1000.0);
  check_nan("ppm_from_vendor_curve(NAN) stays NaN", ppm_from_vendor_curve(NAN, H2));

  std::printf("\n== Vendor model: the other gases ==\n");
  check_close("CO threshold 0.425 -> 0 ppm", ppm_from_vendor_curve(0.425f, CO), 0.0);
  check_close("CO ratio 0.4 -> 61.7 ppm", ppm_from_vendor_curve(0.4f, CO), (0.425 - 0.4) / 0.000405, 1e-3);
  check_close("NH3 below its 10 ppm minimum -> 0", ppm_from_vendor_curve(0.79f, NH3), 0.0);
  check_close("CH4 below its 1000 ppm minimum -> 0", ppm_from_vendor_curve(0.77f, CH4), 0.0);
  check_close("CH4 ratio 0.7629 -> 1004.3 ppm", ppm_from_vendor_curve(0.7629f, CH4), (0.786 - 0.7629) / 0.000023,
              1e-2);

  std::printf("\n== Vendor model: end to end on a 5 V module ==\n");
  const float x_air = air_value_from_voltage(0.5f, 5.0f);  // 4.5 V of headroom in clean air
  check_close("clean-air reference x", x_air, 4.5);
  check_close("V_AO 3.2 V is 'not detected' for H2",
              ppm_from_vendor_curve(ratio_from_air_value(air_value_from_voltage(3.2f, 5.0f), x_air), H2), 0.0);
  check_close("V_AO 4.5 V -> ~645.7 ppm H2",
              ppm_from_vendor_curve(ratio_from_air_value(air_value_from_voltage(4.5f, 5.0f), x_air), H2),
              (0.279 - 0.5 / 4.5) / 0.00026, 0.5);

  std::printf("\n== Datasheet model: RS = (VCC * RL) / V - RL, ppm = a * ratio^b ==\n");
  check_close("rs_from_voltage(2.5 V, VCC 5 V, RL 10 kOhm)", rs_from_voltage(2.5f, 5.0f, 10.0f), 10.0);
  check_close("rs_from_voltage(VCC) is the saturated end (0)", rs_from_voltage(5.0f, 5.0f, 10.0f), 0.0);
  check_close("MAX_RS_OVER_RL is 200", MAX_RS_OVER_RL, 200.0, 1e-6);
  check_close("datasheet_dead_input_voltage(5 V)", datasheet_dead_input_voltage(5.0f), 5.0 / 201.0, 1e-6);
  check_nan("rs_from_voltage(0 V) is a disconnected input -> NaN", rs_from_voltage(0.0f, 5.0f, 10.0f));
  check_nan("rs_from_voltage(negative) is NaN", rs_from_voltage(-1.0f, 5.0f, 10.0f));
  check_nan("rs_from_voltage(above VCC) is NaN", rs_from_voltage(5.5f, 5.0f, 10.0f));
  check_nan("rs_from_voltage(RL 0) is NaN", rs_from_voltage(2.5f, 5.0f, 0.0f));
  check_close("ratio_from_rs(10, 10) is 1", ratio_from_rs(10.0f, 10.0f), 1.0);
  check_close("ratio_from_rs(0, 10) is the saturated end (0)", ratio_from_rs(0.0f, 10.0f), 0.0);
  check_nan("ratio_from_rs(10, 0) is NaN", ratio_from_rs(10.0f, 0.0f));
  check_nan("ratio_from_rs(-1, 10) is NaN", ratio_from_rs(-1.0f, 10.0f));

  // CO: the two-point fit of the datasheet curve (10 ppm @ RS/R0 0.5,
  // 1000 ppm @ RS/R0 0.01) quoted in the Home Assistant community thread.
  const float co_a = 6.3f;
  const float co_b = -1.1f;
  check_close("CO a * (RS/R0)^b reproduces the two-point fit at ratio 0.5", ppm_from_power_law(co_a, co_b, 0.5f),
              6.3 * std::pow(0.5, -1.1), 1e-3);
  check_close("CO at ratio 0.5 is ~13.5 ppm (fit vs datasheet 10 ppm)", ppm_from_power_law(co_a, co_b, 0.5f), 13.48,
              0.05);
  check_close("CO at ratio 0.01 is ~998.5 ppm (fit vs datasheet 1000 ppm)", ppm_from_power_law(co_a, co_b, 0.01f),
              998.5, 0.5);
  check_close("ratio 1 (= clean air, R0 = RS_air) returns a", ppm_from_power_law(co_a, co_b, 1.0f), 6.3, 1e-3);
  check_inf("ratio 0 is infinity on a falling curve (clamped later)", ppm_from_power_law(co_a, co_b, 0.0f));
  check_nan("a = 0 is guarded -> NaN", ppm_from_power_law(0.0f, co_b, 3.0f));
  check_nan("negative ratio is guarded -> NaN", ppm_from_power_law(co_a, co_b, -1.0f));
  check_inf("overflow -> infinity (clamp_ppm makes it max_ppm)", ppm_from_power_law(1e30f, 40.0f, 1e5f));
  check_close("underflow -> 0 ppm", ppm_from_power_law(1e20f, -40.0f, 1e30f), 0.0);

  // R0 round trip: calibrating in clean air makes the ratio 1 and the reading a.
  const float rs_air = rs_from_voltage(2.5f, 5.0f, 10.0f);
  check_close("R0 = RS_air gives ratio 1 in clean air", ratio_from_rs(rs_air, rs_air), 1.0, 1e-6);

  std::printf("\n== Clamps ==\n");
  check_close("clamp_ppm(5000, 0, 1000) -> 1000", clamp_ppm(5000.0f, 0.0f, 1000.0f), 1000.0);
  check_close("clamp_ppm(-1, 0, 1000) -> 0", clamp_ppm(-1.0f, 0.0f, 1000.0f), 0.0);
  // Below the vendor floor counts as "not detected", not as the floor itself.
  check_close("clamp_ppm(0.5, 1, 1000) -> 0", clamp_ppm(0.5f, 1.0f, 1000.0f), 0.0);
  check_close("clamp_ppm(1, 1, 1000) stays 1", clamp_ppm(1.0f, 1.0f, 1000.0f), 1.0);
  check_close("clamp_ppm(FLT_MAX, 0, 1000) -> 1000", clamp_ppm(FLT_MAX, 0.0f, 1000.0f), 1000.0);
  check_close("clamp_ppm(INFINITY, 0, 1000) -> 1000", clamp_ppm(INFINITY, 0.0f, 1000.0f), 1000.0);
  check_close("clamp_ppm(500, 0, INFINITY) stays 500", clamp_ppm(500.0f, 0.0f, INFINITY), 500.0);
  check_nan("clamp_ppm(NAN) stays NAN", clamp_ppm(NAN, 0.0f, 1000.0f));
  check_nan("clamp_ppm(INFINITY, 0, INFINITY) is NaN", clamp_ppm(INFINITY, 0.0f, INFINITY));

  std::printf("\n== Clean-air calibration guard ==\n");
  check_close("MAX_CALIBRATION_SPREAD is 0.25", MAX_CALIBRATION_SPREAD, 0.25, 1e-6);
  check_true("samples 4.4 - 5.6 V around 5.0 V are stable", calibration_is_stable(4.4f, 5.6f, 5.0f));
  check_true("samples 4.3 - 5.8 V around 5.0 V are not", !calibration_is_stable(4.3f, 5.8f, 5.0f));
  check_true("a flat calibration is stable", calibration_is_stable(5.0f, 5.0f, 5.0f));
  check_true("a NaN sample is not stable", !calibration_is_stable(NAN, 5.6f, 5.0f));
  check_true("a non-positive mean is not stable", !calibration_is_stable(0.0f, 0.0f, 0.0f));

  if (failures == 0) {
    std::printf("\nAll mics_math tests passed.\n");
    return EXIT_SUCCESS;
  }
  std::printf("\n%d mics_math test(s) FAILED.\n", failures);
  return EXIT_FAILURE;
}

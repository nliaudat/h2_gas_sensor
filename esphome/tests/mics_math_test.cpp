// Host side validation of components/mics_5524_gas_sensor/mics_math.h
//
// Build & run (no ESPHome/ESP-IDF needed):
//     g++ -std=c++17 -O2 -Wall -Wextra -I ../components/mics_5524_gas_sensor mics_math_test.cpp -o mics_math_test
//     ./mics_math_test
//
// The expected values are recomputed here from the published sources, so the
// test doubles as a check that the ported formulas still match them:
//   * vendor model  -> DFRobot_MICS (MIT), src/DFRobot_MICS.cpp getGasData()
//   * datasheet model -> CO a=6.3 / b=-1.1, the two-point fit of the datasheet
//     curve quoted in the Home Assistant community thread (see
//     docs/mics5524_conversion.md)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "mics_math.h"

using namespace esphome::mics_5524_gas_sensor::micsmath;

static int failures = 0;

static void check_close(const char *name, double actual, double expected, double eps = 1e-4) {
  const double scale = std::max(std::fabs(actual), std::fabs(expected));
  const bool ok =
      std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual - expected) <= eps + 1e-6 * scale;
  std::printf("%-58s %-14s got=%.6g expected=%.6g\n", name, ok ? "OK" : "FAIL", actual, expected);
  if (!ok)
    failures++;
}

static void check_true(const char *name, bool ok) {
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
  check_close("air_value_from_voltage(above VCC) is clamped to 0", air_value_from_voltage(5.5f, 5.0f), 0.0);
  check_close("air_value_from_voltage(NAN) is 0", air_value_from_voltage(NAN, 5.0f), 0.0);
  check_close("ratio_from_air_value(2, 4)", ratio_from_air_value(2.0f, 4.0f), 0.5);
  check_close("ratio_from_air_value(x, 0) is 0", ratio_from_air_value(2.0f, 0.0f), 0.0);
  check_close("ratio_from_air_value(NAN, 4) is 0", ratio_from_air_value(NAN, 4.0f), 0.0);

  std::printf("\n== Vendor model: H2 (threshold 0.279, gain 0.00026, 1..1000 ppm) ==\n");
  check_close("clean air (ratio 1.0) is 'not detected' -> 0 ppm", ppm_from_vendor_curve(1.0f, H2), 0.0);
  check_close("ratio at the threshold -> 0 ppm", ppm_from_vendor_curve(0.279f, H2), 0.0);
  // Note: a reading within one vendor gain step of the minimum rounds to 0 ppm
  // (the vendor clamps everything below min_ppm), so exact checks use multiples
  // of the gain that are unambiguous in float arithmetic.
  check_close("two gain steps below the threshold -> 2 ppm", ppm_from_vendor_curve(0.279f - 2.0f * 0.00026f, H2), 2.0,
              1e-3);
  check_close("just inside the 1 ppm limit -> ~1.05 ppm", ppm_from_vendor_curve(0.279f - 1.05f * 0.00026f, H2), 1.05,
              1e-3);
  check_close("ratio 0.2788 (0.77 ppm) is below the 1 ppm limit -> 0", ppm_from_vendor_curve(0.2788f, H2), 0.0);
  check_close("ratio 0.1111 -> 645.8 ppm", ppm_from_vendor_curve(0.1111f, H2), (0.279 - 0.1111) / 0.00026, 1e-2);
  check_close("ratio at the top of the range -> 1000 ppm (max)", ppm_from_vendor_curve(0.019f, H2), 1000.0, 1e-2);
  check_close("below the range is clamped to max_ppm", ppm_from_vendor_curve(0.018f, H2), 1000.0);
  check_close("ratio 0 is guarded -> 0 ppm", ppm_from_vendor_curve(0.0f, H2), 0.0);
  check_true("NAN ratio stays NAN (caller publishes unknown)", std::isnan(ppm_from_vendor_curve(NAN, H2)));

  std::printf("\n== Vendor model: the other gases ==\n");
  check_close("CO two gain steps below its threshold -> 2 ppm", ppm_from_vendor_curve(0.425f - 2.0f * 0.000405f, CO),
              2.0, 1e-3);
  check_close("CO 0.98 ppm is below the 1 ppm limit -> 0", ppm_from_vendor_curve(0.4246f, CO), 0.0);
  check_close("CO ratio 0.02 -> 1000 ppm (max)", ppm_from_vendor_curve(0.02f, CO), 1000.0, 1e-2);
  check_close("NH3 20 gain steps below the threshold -> 20 ppm", ppm_from_vendor_curve(0.8f - 20.0f * 0.0015f, NH3),
              20.0, 1e-3);
  check_close("NH3 below its 10 ppm minimum -> 0", ppm_from_vendor_curve(0.79f, NH3), 0.0);
  check_close("CH4 below its 1000 ppm minimum -> 0", ppm_from_vendor_curve(0.77f, CH4), 0.0);
  check_close("CH4 ratio 0.7629 -> 1004.3 ppm", ppm_from_vendor_curve(0.7629f, CH4), (0.786 - 0.7629) / 0.000023, 1e-2);

  std::printf("\n== Vendor model: end to end on a 5 V module ==\n");
  const float x_air = air_value_from_voltage(0.5f, 5.0f);  // 4.5 V of headroom in clean air
  check_close("clean-air reference x", x_air, 4.5);
  check_close("V_AO 3.2 V is 'not detected' for H2",
              ppm_from_vendor_curve(ratio_from_air_value(air_value_from_voltage(3.2f, 5.0f), x_air), H2), 0.0);
  check_close("V_AO 4.5 V -> ~646 ppm H2",
              ppm_from_vendor_curve(ratio_from_air_value(air_value_from_voltage(4.5f, 5.0f), x_air), H2),
              (0.279 - 0.5 / 4.5) / 0.00026, 0.5);

  std::printf("\n== Datasheet model: RS = (VCC * RL) / V - RL, ppm = a * ratio^b ==\n");
  check_close("rs_from_voltage(2.5 V, VCC 5 V, RL 10 kOhm)", rs_from_voltage(2.5f, 5.0f, 10.0f), 10.0);
  check_close("rs_from_voltage(0 V) is clamped to 0", rs_from_voltage(0.0f, 5.0f, 10.0f), 0.0);
  check_close("rs_from_voltage(negative) is clamped to 0", rs_from_voltage(-1.0f, 5.0f, 10.0f), 0.0);
  check_close("ratio_from_rs(10, 10) is 1", ratio_from_rs(10.0f, 10.0f), 1.0);
  check_close("ratio_from_rs(10, 0) is 0", ratio_from_rs(10.0f, 0.0f), 0.0);

  // CO: the two-point fit of the datasheet curve (10 ppm @ RS/R0 0.5,
  // 1000 ppm @ RS/R0 0.01) quoted in the Home Assistant community thread.
  const float co_a = 6.3f;
  const float co_b = -1.1f;
  check_close("CO a * (RS/R0)^b reproduces 10 ppm at ratio 0.5 (2-point fit)", ppm_from_power_law(co_a, co_b, 0.5f),
              6.3 * std::pow(0.5, -1.1), 1e-3);
  check_close("CO at ratio 0.5 is ~13.5 ppm (fit vs datasheet 10 ppm)", ppm_from_power_law(co_a, co_b, 0.5f), 13.48,
              0.05);
  check_close("CO at ratio 0.01 is ~998 ppm (fit vs datasheet 1000 ppm)", ppm_from_power_law(co_a, co_b, 0.01f),
              6.3 * std::pow(0.01, -1.1), 1e-2);
  check_close("CO at ratio 0.01 ~ 998.5 ppm", ppm_from_power_law(co_a, co_b, 0.01f), 998.5, 0.5);
  check_close("ratio 1 (= clean air, R0 = RS_air) returns a", ppm_from_power_law(co_a, co_b, 1.0f), 6.3, 1e-3);
  check_close("ratio 0 is guarded -> 0 ppm", ppm_from_power_law(co_a, co_b, 0.0f), 0.0);
  check_close("a = 0 is guarded -> 0 ppm", ppm_from_power_law(0.0f, co_b, 3.0f), 0.0);
  check_close("overflow -> FLT_MAX", ppm_from_power_law(1e30f, 40.0f, 1e5f), FLT_MAX);
  check_close("underflow -> 0 ppm", ppm_from_power_law(1e20f, -40.0f, 1e30f), 0.0);

  // R0 round trip: calibrating in clean air makes the ratio 1 and the reading a.
  const float rs_air = rs_from_voltage(2.5f, 5.0f, 10.0f);
  check_close("R0 = RS_air gives ratio 1 in clean air", ratio_from_rs(rs_air, rs_air), 1.0, 1e-6);

  std::printf("\n== Clamps ==\n");
  check_close("clamp_ppm(5000, 0, 1000) -> 1000", clamp_ppm(5000.0f, 0.0f, 1000.0f), 1000.0);
  check_close("clamp_ppm(-1, 0, 1000) -> 0", clamp_ppm(-1.0f, 0.0f, 1000.0f), 0.0);
  check_close("clamp_ppm(FLT_MAX, 0, 1000) -> 1000", clamp_ppm(FLT_MAX, 0.0f, 1000.0f), 1000.0);
  check_true("clamp_ppm(NAN) stays NAN", std::isnan(clamp_ppm(NAN, 0.0f, 1000.0f)));
  check_close("safe_pow(x, 0) = 1", safe_pow(3.0, 0.0), 1.0);
  check_true("will_overflow(1e6)", will_overflow(1e6));
  check_true("!will_overflow(2)", !will_overflow(2.0));

  if (failures == 0) {
    std::printf("\nAll mics_math tests passed.\n");
    return EXIT_SUCCESS;
  }
  std::printf("\n%d mics_math test(s) FAILED.\n", failures);
  return EXIT_FAILURE;
}

// Host side validation of components/mq_gas_sensors/mq_math.h
//
// Build & run (no ESPHome/ESP-IDF needed):
//     g++ -std=c++17 -O2 -I ../components/mq_gas_sensors mq_math_test.cpp -o mq_math_test
//     ./mq_math_test
//
// The expected values are recomputed here with plain <cmath> calls, so the test
// doubles as a check that the ported formulas still match MQUnifiedsensor.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "mq_math.h"

using namespace esphome::mq_gas_sensors::mqmath;

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

int main() {
  std::printf("== ADC voltage ==\n");
  check_close("voltage_from_adc(4095, 12 bits, 3.3 V)", voltage_from_adc(4095, 12, 3.3f), 3.3);
  check_close("voltage_from_adc(2048, 12 bits, 3.3 V)", voltage_from_adc(2048, 12, 3.3f), 2048.0 * 3.3 / 4095.0);
  check_close("voltage_from_adc(1023, 10 bits, 5 V)", voltage_from_adc(1023, 10, 5.0f), 5.0);

  std::printf("\n== RS from the AO voltage (RS = (VCC*RL)/V - RL) ==\n");
  const double v = 2048.0 * 3.3 / 4095.0;
  check_close("rs_from_voltage(V, VCC=5, RL=10)", rs_from_voltage(v, 5.0f, 10.0f), (5.0 * 10.0) / v - 10.0);
  check_close("rs_from_voltage(0 V) is clamped to 0", rs_from_voltage(0.0f, 5.0f, 10.0f), 0.0);
  check_close("rs_from_voltage(negative) is clamped to 0", rs_from_voltage(-1.0f, 5.0f, 10.0f), 0.0);

  std::printf("\n== MQ-8 (H2) curve: a=976.97, b=-0.688, RS/R0 in clean air = 70 ==\n");
  const float mq8_a = 976.97f;
  const float mq8_b = -0.688f;
  check_close("ppm at ratio = 1 equals a", ppm_from_ratio(mq8_a, mq8_b, 1.0f, REGRESSION_EXPONENTIAL), mq8_a);
  check_close("ppm at ratio = 70 (clean air)", ppm_from_ratio(mq8_a, mq8_b, 70.0f, REGRESSION_EXPONENTIAL),
              976.97 * std::pow(70.0, -0.688));
  check_close("ppm at ratio = 70 (clean air) ~ 52.5 ppm", ppm_from_ratio(mq8_a, mq8_b, 70.0f, REGRESSION_EXPONENTIAL),
              52.47, 0.5);
  check_close("ppm at ratio = 0.5 (H2 up)", ppm_from_ratio(mq8_a, mq8_b, 0.5f, REGRESSION_EXPONENTIAL),
              976.97 * std::pow(0.5, -0.688));

  std::printf("\n== R0 calibration (R0 = RS_air / ratio_in_clean_air) ==\n");
  const double rs_air = (5.0 * 10.0) / v - 10.0;
  const double r0 = r0_from_clean_air(static_cast<float>(rs_air), 70.0f, 0.0f);
  check_close("r0_from_clean_air(RS_air, 70)", r0, rs_air / 70.0);
  check_close("ratio back in clean air is 70",
              ratio_from_rs(static_cast<float>(rs_air), static_cast<float>(r0), 0.0f, RATIO_RS_R0), 70.0, 1e-3);
  check_close(
      "clean air reading round trips to ~52 ppm",
      ppm_from_ratio(mq8_a, mq8_b, ratio_from_rs(static_cast<float>(rs_air), static_cast<float>(r0), 0.0f, RATIO_RS_R0),
                     REGRESSION_EXPONENTIAL),
      52.47, 0.5);
  check_close("r0_from_clean_air(0, 70) is 0", r0_from_clean_air(0.0f, 70.0f, 0.0f), 0.0);
  check_close("r0_from_clean_air(RS, 0) is 0", r0_from_clean_air(10.0f, 0.0f, 0.0f), 0.0);

  std::printf("\n== R0/RS mode (MQUnifiedsensor readSensorR0Rs) ==\n");
  check_close("ratio R0/RS = 1/70 in clean air",
              ratio_from_rs(static_cast<float>(rs_air), static_cast<float>(r0), 0.0f, RATIO_R0_RS), 1.0 / 70.0, 1e-6);

  std::printf("\n== Linear regression (MQ-131 O3: a=0.41195, b=-0.4708) ==\n");
  check_close("ppm at ratio = 1", ppm_from_ratio(0.41195f, -0.4708f, 1.0f, REGRESSION_LINEAR),
              std::pow(10.0, (std::log10(1.0) + 0.4708) / 0.41195));
  check_close("ppm at ratio = 1 ~ 13.9", ppm_from_ratio(0.41195f, -0.4708f, 1.0f, REGRESSION_LINEAR), 13.9, 0.1);

  std::printf("\n== Linear regression with negative a (MQ-135 NH3: a=-0.47712, b=0.4491) ==\n");
  check_close("ppm at ratio = 1", ppm_from_ratio(-0.47712f, 0.4491f, 1.0f, REGRESSION_LINEAR),
              std::pow(10.0, (0.0 - 0.4491) / -0.47712));

  std::printf("\n== Guards and clamps ==\n");
  check_close("ratio 0 -> 0 ppm", ppm_from_ratio(mq8_a, mq8_b, 0.0f, REGRESSION_EXPONENTIAL), 0.0);
  check_close("a = 0 -> 0 ppm", ppm_from_ratio(0.0f, mq8_b, 3.0f, REGRESSION_EXPONENTIAL), 0.0);
  // MQ-3 CH4 curve (a=2e31, b=19.01): 2e31 * 10^19.01 exceeds FLT_MAX -> FLT_MAX
  check_close("log overflow -> FLT_MAX", ppm_from_ratio(2e31f, 19.01f, 10.0f, REGRESSION_EXPONENTIAL), FLT_MAX);
  // b=max, tiny ratio -> 10^-3800 underflows -> 0
  check_close("log underflow -> 0 ppm", ppm_from_ratio(1.0f, 100.0f, 1e-38f, REGRESSION_EXPONENTIAL), 0.0);
  check_close("clamp_a(NAN) -> 0", clamp_a(NAN), 0.0);
  check_close("clamp_a(> MQ_MAX_A) -> MQ_MAX_A", clamp_a(static_cast<float>(MQ_MAX_A * 10.0)), MQ_MAX_A);
  check_close("clamp_a(< -MQ_MAX_A) -> -MQ_MAX_A", clamp_a(static_cast<float>(-MQ_MAX_A * 10.0)), -MQ_MAX_A);
  check_close("clamp_b(> MQ_MAX_B) -> MQ_MAX_B", clamp_b(static_cast<float>(MQ_MAX_B * 10.0)), MQ_MAX_B);
  check_close("clamp_ppm(50000, 0, 10000) -> 10000", clamp_ppm(50000.0f, 0.0f, 10000.0f), 10000.0);
  check_close("clamp_ppm(FLT_MAX, 0, 10000) -> 10000", clamp_ppm(FLT_MAX, 0.0f, 10000.0f), 10000.0);
  check_close("clamp_ppm(-1, 0, 10000) -> 0", clamp_ppm(-1.0f, 0.0f, 10000.0f), 0.0);
  check_true("clamp_ppm(NAN) stays NAN", std::isnan(clamp_ppm(NAN, 0.0f, 10000.0f)));
  check_close("safe_pow(x, 0) = 1", safe_pow(3.0, 0.0), 1.0);
  check_close("safe_pow(x, 2) = x*x", safe_pow(3.0, 2.0), 9.0);
  check_true("will_overflow(1e6)", will_overflow(1e6));
  check_true("!will_overflow(2)", !will_overflow(2.0));

  std::printf("\n== MQDataScience inverse regression (method 3) ==\n");
  // Their MQ-8 H2 curve: a = 18391.5667, b = -1.4494, PPM = (ratio / a)^(1 / b).
  const float ds_a = 18391.5667f;
  const float ds_b = -1.4494f;
  check_close("inverse: ppm at ratio = 70 (clean air)", ppm_from_ratio(ds_a, ds_b, 70.0f, REGRESSION_INVERSE),
              std::pow(70.0 / 18391.5667, 1.0 / -1.4494));
  check_close("inverse: ppm at ratio = 70 ~ 46.7 (published value)",
              ppm_from_ratio(ds_a, ds_b, 70.0f, REGRESSION_INVERSE), 46.7, 0.5);
  check_close("inverse: ppm at ratio = 10", ppm_from_ratio(ds_a, ds_b, 10.0f, REGRESSION_INVERSE),
              std::pow(10.0 / 18391.5667, 1.0 / -1.4494));
  check_close("inverse: ppm at ratio = 1", ppm_from_ratio(ds_a, ds_b, 1.0f, REGRESSION_INVERSE), 875.4, 5.0);
  // A' = a^(-1/b), b' = 1/b  =>  a' * ratio^b' reproduces the inverse form.
  check_close("inverse == exponential with a'=875.4, b'=-0.68994 (ratio 70)",
              ppm_from_ratio(ds_a, ds_b, 70.0f, REGRESSION_INVERSE), 875.4 * std::pow(70.0, -0.68994), 0.1);
  check_close("inverse == exponential with a'=875.4, b'=-0.68994 (ratio 1)",
              ppm_from_ratio(ds_a, ds_b, 1.0f, REGRESSION_INVERSE), 875.4 * std::pow(1.0, -0.68994), 0.5);
  check_close("inverse: the two MQ-8 datasets differ by ~11 % in clean air",
              100.0 * (ppm_from_ratio(ds_a, ds_b, 70.0f, REGRESSION_INVERSE) /
                           ppm_from_ratio(mq8_a, mq8_b, 70.0f, REGRESSION_EXPONENTIAL) -
                       1.0),
              -11.1, 0.2);
  check_close("inverse: ratio 0 -> 0 ppm", ppm_from_ratio(ds_a, ds_b, 0.0f, REGRESSION_INVERSE), 0.0);
  check_close("inverse: a = 0 -> 0 ppm", ppm_from_ratio(0.0f, ds_b, 3.0f, REGRESSION_INVERSE), 0.0);

  std::printf("\n== Temperature/humidity correction (MQDataScience Correction.cpp) ==\n");
  // MQ-8 constants: a 0.8559 -> 0.8201, b -0.0611 -> -0.0606, c 0.1673 -> 0.1492.
  const TcCorrectionCoefficients mq8_tc{0.8559f, -0.0611f, 0.1673f, 0.8201f, -0.0606f, 0.1492f};
  // ppm scales with correction**|b'|, i.e. correction**(1 / 1.4494) = correction**0.68994.
  const double tc_scale_exponent = 0.68994;

  check_close("linear_interpolate at x0", linear_interpolate(33.0f, 33.0f, 85.0f, 1.0f, 3.0f), 1.0);
  check_close("linear_interpolate at x1", linear_interpolate(85.0f, 33.0f, 85.0f, 1.0f, 3.0f), 3.0);
  check_close("linear_interpolate at the midpoint", linear_interpolate(59.0f, 33.0f, 85.0f, 1.0f, 3.0f), 2.0);
  check_close("linear_interpolate with x1 == x0", linear_interpolate(59.0f, 33.0f, 33.0f, 1.0f, 3.0f), 1.0);

  check_close("correction(RH 40, 20 degC) ~ 0.8997 (documented value)", correction_coefficient(40.0f, 20.0f, mq8_tc),
              0.8997, 1e-3);
  // At the RH midpoint (59 %) a, b and c are the arithmetic means of the table.
  check_close("correction(RH 59, 20 degC) uses the interpolated midpoints",
              correction_coefficient(59.0f, 20.0f, mq8_tc),
              (0.8559 + 0.8201) / 2.0 + 0.5 * (0.1673 + 0.1492) * std::exp(0.5 * (-0.0611 - 0.0606) * 20.0), 1e-3);
  check_close("correction at RH 33, T 20 (table edge)", correction_coefficient(33.0f, 20.0f, mq8_tc),
              0.8559 + 0.1673 * std::exp(-0.0611 * 20.0), 1e-5);
  check_close("correction at RH 85, T 20 (table edge)", correction_coefficient(85.0f, 20.0f, mq8_tc),
              0.8201 + 0.1492 * std::exp(-0.0606 * 20.0), 1e-5);
  check_close("RH below 33 is clamped", correction_coefficient(10.0f, 20.0f, mq8_tc),
              correction_coefficient(33.0f, 20.0f, mq8_tc));
  check_close("RH above 85 is clamped", correction_coefficient(120.0f, 20.0f, mq8_tc),
              correction_coefficient(85.0f, 20.0f, mq8_tc));
  check_close("T below -10 is clamped", correction_coefficient(50.0f, -40.0f, mq8_tc),
              correction_coefficient(50.0f, -10.0f, mq8_tc));
  check_close("T above 50 is clamped", correction_coefficient(50.0f, 80.0f, mq8_tc),
              correction_coefficient(50.0f, 50.0f, mq8_tc));
  check_true("missing humidity (NAN) -> no correction", correction_coefficient(NAN, 20.0f, mq8_tc) == 1.0f);
  check_true("missing temperature (NAN) -> no correction", correction_coefficient(40.0f, NAN, mq8_tc) == 1.0f);
  check_true("infinite temperature -> no correction", correction_coefficient(40.0f, INFINITY, mq8_tc) == 1.0f);

  std::printf("\n== Correction application (ratio / correction) ==\n");
  check_close("apply_correction(70, 0.9)", apply_correction(70.0f, 0.9f), 70.0 / 0.9);
  check_close("apply_correction(70, 1) is a no-op", apply_correction(70.0f, 1.0f), 70.0);
  check_close("apply_correction(70, 0) is a no-op", apply_correction(70.0f, 0.0f), 70.0);
  check_close("apply_correction(70, NAN) is a no-op", apply_correction(70.0f, NAN), 70.0);
  check_true("apply_correction(NAN, 0.9) stays NAN", std::isnan(apply_correction(NAN, 0.9f)));

  const float corr_40_20 = correction_coefficient(40.0f, 20.0f, mq8_tc);
  check_close("clean air (ratio 70) at RH 40 / 20 degC: ~48.8 instead of 52.5 ppm",
              ppm_from_ratio(mq8_a, mq8_b, apply_correction(70.0f, corr_40_20), REGRESSION_EXPONENTIAL),
              976.97 * std::pow(70.0 / 0.8997, -0.688), 0.5);
  check_close("ppm scaling exponent used by the docs is |b'| = 0.68994",
              100.0 * (std::pow(static_cast<double>(corr_40_20), tc_scale_exponent) - 1.0), -7.0, 0.2);
  // An uncorrected reading must be bit-identical to the regression alone.
  check_close("correction = 1 reproduces the uncorrected curve",
              ppm_from_ratio(mq8_a, mq8_b, apply_correction(70.0f, 1.0f), REGRESSION_EXPONENTIAL),
              976.97 * std::pow(70.0, -0.688));

  if (failures == 0) {
    std::printf("\nAll mq_math tests passed.\n");
    return EXIT_SUCCESS;
  }
  std::printf("\n%d mq_math test(s) FAILED.\n", failures);
  return EXIT_FAILURE;
}

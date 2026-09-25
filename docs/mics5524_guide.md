# MiCS-5524 guide - hardware, wiring and calibration

Component reference:
[`../esphome/components/mics_5524_gas_sensor/README.md`](../esphome/components/mics_5524_gas_sensor/README.md).
Formulas and provenance: [`mics5524_conversion.md`](mics5524_conversion.md).

## What it is (and is not)

A MEMS metal-oxide sensor with a **single analog output** for several reducing
gases. Sold as a small module with four pads: `VCC`, `GND`, `A0` (analog output)
and `EN` (enable, sometimes `PD`).

* **Is**: a cheap, sensitive *trend* sensor for the trace band (100 - 1000 ppm of
  H2 with this component's H2 curve) - exactly the early-warning role this project
  wants in front of the MQ-8.
* **Is not**: selective, calibrated, or safety-grade. One output equals one
  mixture of CO, H2, ethanol, ammonia and methane; the community thread reports
  widely differing offsets, occasionally stuck readings, and the vendor itself
  says "calibration is required, do not use it for safety".

## Wiring (5 V analog module)

```
module 5V  -> 5 V (the devkit's 5V pin, ~90 mA during the heater cycle)
module GND -> GND (same node as the ESP32)
module A0  -> divider -> ADC1 pin          (or directly into an ADS1115 input)
module EN  -> GPIO (+ inverted: true)      (LOW = enabled on the Fermion and clones)
```

* The output swings up to 5 V while the ESP32 ADC stops measuring correctly
  around 3.1-3.3 V with `attenuation: 12db`, so use a divider - **10 kOhm in
  series / 20 kOhm to ground** (`divider: {r1: 10.0, r2: 20.0}` -> multiplier
  1.5) maps 5 V to 3.33 V. The vendor model only needs the ratio to be
  *compensated*, not to be a specific value.
* **The ESP32 ADC pins are not 5 V tolerant** (absolute maximum VDD + 0.3 V =
  3.6 V): never wire `A0` straight to a GPIO, and do not treat the series
  resistor as protection. 3.33 V is a hair above the ESP32's 3.3 V recommended
  maximum, which is why the packages declare `adc_input_max: 3.33` (the very top
  of the range may read non-linearly). Use 10k/10k (5 V -> 2.5 V) if you care
  about the top of the range, or an ADS1115. The component rejects configurations
  that could exceed `adc_pin_max` (3.6 V) at compile time.
* Use an ADC1 pin (GPIO32-39); ADC2 is unavailable while Wi-Fi is active.
* **ADS1115 alternative** (recommended if you want resolution): the 16-bit
  ADS1115 with gain `6.144 V` accepts the 0-5 V output directly (no divider) and
  is a `voltage_sampler` in ESPHome, so it can be used as `voltage:` - see the
  community thread post 13 for a wiring example (`Vout -> A2`, `EN -> GPIO4`).
* `EN` on the Fermion breakout and on most clones must be pulled **low** to enable
  the sensor (DFRobot's own driver writes the pin low to wake it), hence
  `enable_pin: { number: GPIO4, inverted: true }`. If your board has no `EN` pad or
  it is already pulled low, remove `enable_pin:`.
* The vendor model does not need the board's load resistor at all. Only if you use
  `conversion: datasheet` must you know it: measure the resistor between `A0` and
  `GND` (the common boards use 10 kOhm, the value the community thread assumes)
  and set `rl:` accordingly.

## Warm-up, burn-in and calibration

1. **Heater warm-up: 3 minutes** (`warmup_time: 3min`). The vendor example also
   warns: do not touch the probe while it warms up, and keep the sensor in clean
   air during that time.
2. **Burn-in: 24 - 48 h** for a brand-new sensor before you trust the values
   (`warmup_time: 24h` suppresses everything during that window).
3. **Calibration in clean air** (`calibration:`) stores the reference - the
   vendor model's `x_air = VCC - V_AO`, the datasheet model's `R0 = RS`. It runs
   after `max(delay, warmup_time)`, averages `samples` readings (the vendor library
   averages 10) and is kept in flash. Pin it later with `air_reference:`/`r0:` to
   skip the calibration at boot.
4. **A reference is only valid if it was captured with a settled sensor.** The
   vendor model normalises against `x_air`, so its ratio is 1.0 *at the moment of
   the calibration* and nowhere else. A calibration that ran while the module was
   still warming up - or during the burn-in of a new sensor - stores a reference
   that is too high: the ratio then sits below 1.0 in clean air, and the vendor
   curve **over-reports** (it starts firing earlier and shows too many ppm).
   Worked example: a reference captured at `V_AO = 0.21 V` gives
   `x_air = 5.0 - 0.21 = 4.79 V`, so a module that settles at `V_AO = 1.10 V`
   (`x = 3.90 V`) reports `ratio = 3.90 / 4.79 = 0.81` instead of 1.0 - and at
   `V_AO = 4.0 V` the H2 curve returns `(0.279 - 1.0 / 4.79) / 0.00026 = 270 ppm`
   where a reference captured at 1.10 V returns 86 ppm. Check the ratio right
   after a calibration (≈ 1.0) and re-calibrate if it drifts away.
5. Re-calibrate after changing the wiring or `voltage_multiplier`, and every few
   months: these sensors drift. The *MiCS-5524 recalibrate* button in Home
   Assistant (or `id(mics).request_calibration()`) does it; a request is deferred
   until `warmup_time` has elapsed and the state stays `unknown` while the
   calibration is pending or running.

## ESP32 ADC limitations

* Below ≈ 75 mV the ADC of a classic ESP32 cannot measure reliably, which the
  community thread translates to ≈ 7 ppm CO - and to an equivalent floor for H2.
  Fuel the module through a divider that keeps the interesting range above that,
  or use an ADS1115.
* ESP32 ADC readings are noisy: `samples: 4` (default) averages four conversions
  per update, `samples: 16` is worth trying if your values jitter. A 100 nF
  capacitor from the ADC pin to ground also helps.
* The shipped package polls the H2 trace entry and its diagnostics every second
  (`mics_update_interval`, `mics_diag_interval`) - the four extra gas views are the
  same signal through other curves, so they carry their own key
  (`mics_extra_gas_update_interval`) and can be slowed down (e.g. `30s`) without
  touching the alarm entity.  Raise `samples:` if the raw value matters more than
  the response time.

## Role in this project (H2 battery room)

| Sensor | Band | Role |
|---|---|---|
| MiCS-5524 (`packages/mics5524.yaml`) | 100 - 1000 ppm (0.25 - 2.5 % LEL) | trace / early detection, trend before the MQ-8 reacts |
| MQ-8 (`packages/mq8.yaml`) | 4 000 - 10 000 ppm (10 - 25 % LEL) | pre-alarm at the NFPA 855 design target |

Thresholds and the reasoning behind them: [`h2_thresholds.md`](h2_thresholds.md).
Both packages can be included in the same device (they only share the I2C-free
defaults) - include `mics5524` *in addition to* one MQ-8 package, and keep the
alerting in Home Assistant.

```yaml
packages:
  mq8: !include packages/mq8.yaml
  mics5524: !include packages/mics5524.yaml
```

## Publishing several gases

The package instantiates one `mics_5524_gas_sensor` entry per gas of the vendor
table, all reading the same `A0`:

| Entity (suffix of `friendly_name`) | Vendor curve |
|---|---|
| `H2 trace (MiCS-5524)` | H2 - the trace / alarm reference |
| `CO (MiCS-5524)` | CO |
| `NH3 (MiCS-5524)` | NH3 |
| `C2H5OH (MiCS-5524)` | C2H5OH (ethanol) |
| `CH4 (MiCS-5524)` | CH4 (methane) |

The per-gas thresholds, gains and ranges are in
[`mics5524_conversion.md`](mics5524_conversion.md); `conversion: dfrobot` is
required for the four extra gases (only CO also ships with
`conversion: datasheet` coefficients).

**They are not five measurements.** There is a single analog output, so all five
entities are the same `ratio = (VCC - V_AO) / x_air` pushed through five
different curves: a reading shown as some ppm of H2 becomes a different number
when the same signal is read as CO, purely because the curves differ. Each entry
captures and persists its own clean-air reference on the first boot, and the
ratio / scaled-voltage diagnostics are linked to the H2 entry only (the ratio is
identical for all five). Use the extra entities to see how the other gases would
interpret the same signal, never as independent readings - keep the alerting on
`H2 trace` (and the MQ-8).

Only the H2 entry logs its value (`log_ppm: true` in
[`../esphome/packages/mics5524.yaml`](../esphome/packages/mics5524.yaml)): one
`INFO` line per update, e.g. `[I][mics_5524_gas_sensor]: 'H2': 0.0 ppm`.  The
four views stay silent, and the whole chain per gas
(`V_AO=… V, x=…, ratio=… -> … ppm`) is logged at `DEBUG` - set the
`mics_5524_gas_sensor` tag back to `DEBUG` under `logger.logs` to read it
([`troubleshooting.md`](troubleshooting.md#reading-the-log) explains the `[S]`
lines that `esphome logs` adds on top).

## Confidence checklist before trusting a reading

* ratio diagnostic ≈ 1.0 in clean air, and it *drops* when a reducing gas arrives;
* the AO voltage moves when you breathe near the sensor (a cheap sanity test);
* the value returns to ~0 ppm after the gas clears;
* no `analog output reads 0.0000 V` warning in the log.

If any of those fail, re-check the wiring, the EN polarity and the calibration
before looking at the model.

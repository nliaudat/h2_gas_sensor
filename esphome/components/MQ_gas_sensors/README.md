# MQ_gas_sensors

ESPHome sensor platform for **every MQ gas sensor** of the
[MQUnifiedsensor](https://github.com/miguel5612/MQSensorsLib) family
(MQ-2 … MQ-9, MQ-131, MQ-135 … MQ-138, MQ-136, MQ-214, MQ-303A, MQ-309A and
`CUSTOM`), ported from the ESP-IDF component
[`MQSensorLIB`](https://github.com/RapportTecnologia/esp-iot-solution/tree/MQSensorLib/components/sensors/gas/MQSensorLIB)
(`MQUnifiedsensor.cpp`) to a native ESPHome component.

It implements the same measurement chain:

```
V      = adc * volt_resolution / (2^bits - 1)      (an ESPHome `adc` sensor does this for you)
RS     = (VCC * RL) / V - RL                        (RS_Calc / getRS())
ratio  = RS / R0                                    (datasheet convention)
PPM    = a * ratio^b                                regression method "exponential"
log10(PPM) = (log10(ratio) - b) / a                 regression method "linear"
PPM    = (ratio / a)^(1/b)                          regression method "inverse" (MQDataScience)
R0     = RS_air / ratio_in_clean_air                calibrate()
```

Optionally the ratio is compensated for the ambient temperature and humidity
with MQDataScience's model (`correction_mode: mqdatascience`):

```
correction = a + c * exp(b * T)   a/b/c interpolated over RH, T clamped to -10..50 degC
ratio_eff  = ratio / correction
```

plus the reference implementation's guards (`safe_pow`, `will_overflow`,
negative → 0, non-finite → `FLT_MAX`) and the `samples`/`sample_interval`
averaging that `MQUnifiedsensor::getVoltage()` does with `retries`.

## Highlights

* Supports **all MQ sensor types** with the `a` / `b` / regression method /
  clean-air ratio tables built in (see `coefficients.py`) - a wrong
  `sensor_type`/`gas` combination fails the build instead of producing nonsense.
* Optional temperature/humidity compensation (`correction_mode: mqdatascience`,
  needs `temperature:`/`humidity:` sensor ids) ported from
  [MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience) - see
  [`docs/temperature_humidity_correction.md`](../../../docs/temperature_humidity_correction.md).
* Optional alternative coefficient dataset (`curve: mqdatascience`) for a
  side-by-side comparison; the default `curve: standard` is unchanged.
* Analog input from an existing voltage sampler (`voltage:`) **or** from a pin
  this component owns (`pin:` - it creates the hidden `adc` entry for you).
* R0 from the configuration, from a **flash-persisted calibration**, or from an
  automatic **clean-air calibration** after a configurable delay.
* Warm-up/burn-in suppression, PPM range clamps, `correction_factor`, ratio
  direction switch for bit-exact `MQUnifiedsensor::readSensorR0Rs()` parity.
* Optional diagnostic entities: RS, RS/R0 ratio, AO voltage.
* Host-testable math (`tests/mq_math_test.cpp`, no ESPHome needed).

## Minimal configuration

```yaml
# The MQ module needs 5 V; its AO pin feeds the ADC through a divider.
sensor:
  - platform: adc
    id: mq8_adc
    name: "MQ-8 AO voltage"
    pin: GPIO34
    attenuation: 12db          # ~0-3.1 V input range
    update_interval: 30s
    entity_category: diagnostic

  - platform: MQ_gas_sensors
    id: mq8
    name: "H2 concentration"
    sensor_type: MQ-8          # MQ-2, MQ-3, ... MQ-309A, CUSTOM
    gas: H2                    # default: the primary gas of the type
    voltage: mq8_adc           # or: pin: GPIO34
    voltage_multiplier: 1.5    # inverse of the divider ratio (2/3 -> 1.5)
    vcc: 5.0                   # sensor supply
    rl: 10.0                   # module load resistor (kOhm)
    update_interval: 30s
    calibration:
      delay: 5min              # wait for boot + heater stabilisation
      persist: true            # keep R0 in flash, reuse it after a reboot
```

`packages/mq8.yaml` in this repository is a ready-to-use version of the above
(including the diagnostics and the H2 threshold notes).

## Options

| Key | Default | Description |
|---|---|---|
| `sensor_type` | *required* | `MQ-2`, `MQ-3`, `MQ-4`, `MQ-5`, `MQ-6`, `MQ-7`, `MQ-8`, `MQ-9`, `MQ-131`, `MQ-135`, `MQ-136`, `MQ-137`, `MQ-138`, `MQ-214`, `MQ-303A`, `MQ-309A`, `CUSTOM`. `mq8`/`mq-8`/`MQ_8` are accepted and normalised. |
| `gas` | type's primary gas | Gas key of the coefficient table (`H2`, `LPG`, `CH4`, `CO`, `ALCOHOL`, `PROPANE`, `SMOKE`, `BENZENE`, `HEXANE`, `NH3`, `O3`, `ACETONE`, `TOLUENE`). Must exist for the chosen type. |
| `voltage` | – | `id` of a sensor implementing the voltage sampler interface (an `adc` sensor). Exactly one of `voltage`/`pin`. |
| `pin` | – | ADC pin owned by the component; a hidden internal `adc` sensor is generated with `adc_attenuation`/`adc_samples`. Exactly one of `voltage`/`pin`. |
| `adc_attenuation` | `12db` (ESP32) | Only for `pin:` (e.g. `0db`, `2.5db`, `6db`, `11db`, `12db`, `auto`). |
| `adc_samples` | `1` | Only for `pin:`; ADC multisampling of the generated sensor. |
| `voltage_multiplier` | `1.0` | Scales the sampled AO voltage (external divider, e.g. `1.5` for 2/3). |
| `vcc` | `5.0` | Sensor supply voltage in V (used for RS). |
| `rl` | `10.0` | Load resistor of the module in kOhm. |
| `r0` | – | Fixed R0 in kOhm. Disables the automatic calibration. |
| `a`, `b` | from the table | Regression coefficients (`PPM = a * ratio^b`). Required for `CUSTOM`, MQ-136, MQ-214, MQ-303A, MQ-309A. |
| `regression_method` | from the table | `exponential` (default) or `linear`. |
| `ratio_mode` | `rs_r0` | `rs_r0` (datasheet convention, matches the shipped coefficients) or `r0_rs` (`MQUnifiedsensor::readSensorR0Rs()`). |
| `ratio_in_clean_air` | from the table | RS/R0 in clean air, used to compute R0. |
| `samples` | `2` | Number of AO readings averaged per update (the reference library uses 2). |
| `sample_interval` | `20ms` | Delay between those readings. |
| `warmup_time` | `0s` | Burn-in/warm-up: nothing is published (state `unknown`) before it elapses. Use `24h`+ for a new sensor. |
| `min_ppm`, `max_ppm` | table/datasheet | Clamp of the published value (`MQ-8`: 0 … 10 000 ppm). |
| `correction_factor` | `0.0` | Added to the ratio (and to R0) as in `MQUnifiedsensor` (`readSensor(correctionFactor)`). |
| `correction_mode` | `none` | `none` or `mqdatascience` - temperature/humidity compensation of the ratio. Needs `temperature:`/`humidity:` and a type with correction constants (MQ-2 … MQ-8, MQ-135 … MQ-138, MQ-214). |
| `correction_clamp` | `absolute` | `absolute` clips to `max_ppm` (the alarm ceiling does not move); `scaled` clips to `max_ppm * correction` (MQDataScience's own behaviour). |
| `curve` | `standard` | `standard` (SolderedElectronics/MQUnifiedsensor) or `mqdatascience` (their dataset, ~11 % lower for MQ-8 H2). Cannot be combined with `a:`/`b:`. |
| `temperature`, `humidity` | – | `id`s of the ambient temperature (°C) / relative humidity (%) sensors used by `correction_mode`. |
| `correction_sensor` | – | Optional `id` of a sensor receiving the applied correction factor (1.0000 = uncorrected). |
| `calibration` | – | See below. |
| `ratio_sensor`, `rs_sensor`, `voltage_sensor` | – | Optional `id`s of sensors that receive the RS/R0 ratio, RS (kOhm) and AO voltage (V). |
| `update_interval` | `60s` | Normal sensor polling interval. |

With `pin:`, the generated `adc` entry is validated by the ADC platform's own
schema and stored inside this sensor's configuration under the internal
`_generated_adc` key (that is why it shows up in the `esphome config` output).
It is a hidden `adc` sensor without a Home Assistant entity - it is created with
`update_interval: never` and only sampled on demand by this component.

## Calibration

The sensor needs `R0` (its resistance in clean air) before it can compute PPM.
`R0` is used exactly like in the reference library:

```
R0 = RS_air / ratio_in_clean_air
```

where `RS_air` is measured while the sensor sits in clean air. Three ways to
provide it, in order of precedence:

1. **Fixed value** - measure once in the laboratory, then pin it:
   ```yaml
   r0: 0.2899    # kOhm, taken from the "R0 = ..." log line of a previous run
   ```
2. **Automatic calibration, stored in flash** (recommended):
   ```yaml
   calibration:
     ratio_in_clean_air: 70   # RS/R0 from the datasheet (default per type)
     delay: 5min              # after boot (max(delay, warmup_time))
     samples: 50              # number of RS samples
     duration: 0s             # extra time limit, 0 = only the sample count matters
     persist: true            # write R0 to flash and reuse it after a reboot
   ```
   The calibration only runs when `R0` is unknown (first boot, cleared flash or
   `persist: false`). Once a valid `R0` is stored it is restored at boot and the
   calibration is skipped - important for a battery room, where the air is *not*
   clean when hydrogen is present.
3. **On demand** - force a new calibration from an automation:
   ```yaml
   esphome:
     on_boot:
       - delay: 5min
       - lambda: id(mq8).request_calibration();   # sensor must be in clean air
   ```
   The calibrated value is logged (`R0 = ... kOhm`), so it can be pinned with
   `r0:` afterwards if you prefer a static configuration.

Other calibration facts:

* `MQUnifiedsensor::calibrate()` (and therefore this component) needs the
  sensor to be **pre-heated**; the reference documentation recommends 24-48 h of
  burn-in for a new sensor, which is what `warmup_time: 24h` models (nothing is
  published during that window).
* Calibration samples that produce an invalid RS (AO ≈ 0 V, open circuit) are
  reported (`calibration failed, none of the N samples produced a valid RS`)
  and the previous `R0` is kept.

## Temperature/humidity correction (optional)

`correction_mode: mqdatascience` compensates the RS/R0 ratio for the ambient
conditions with the model of
[MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience)
(`Correction.cpp`): `correction = a + c * exp(b * T)`, with `a`/`b`/`c`
interpolated between RH 33 % and RH 85 %, `T` clamped to -10 … 50 °C, applied as
`ratio_eff = ratio / correction`.

* Needs the `temperature:` and `humidity:` sensor ids; the type must have
  constants (`MQ2` … `MQ8`, `MQ135` … `MQ138`, `MQ214`), otherwise `esphome
  config` fails with the list of supported types. MQ-9/MQ-131 use a different
  two-segment model and are not supported.
* For the MQ-8 the factor is 0.8997 at RH 40 %/20 °C (ppm × 0.9297) and stays
  between 0.898 and 0.948 over a normal indoor range - a ±3 % refinement, and
  it shifts the clean-air baseline to ≈ 48.8 ppm instead of 52.5 ppm.
* **Fail-open**: a missing/stale/NaN T/RH reading leaves the value uncorrected
  (factor 1.0000) with a one-time warning - the gas measurement is never
  suppressed or zeroed by an auxiliary sensor.
* `correction_clamp: absolute` (default) keeps `max_ppm` as the ceiling;
  `scaled` reproduces MQDataScience's `max_ppm * correction` (which would lower
  the 10 000 ppm pre-alarm to ≈ 8 900 ppm in warm humid air).
* `ratio_sensor` keeps publishing the *measured* RS/R0; `correction_sensor`
  publishes the factor that was applied.

```yaml
    temperature: air_temperature
    humidity: air_humidity
    correction_mode: mqdatascience
    correction_clamp: absolute
    correction_sensor: mq8_correction
```

Details, constants and the full effect table: `docs/temperature_humidity_correction.md`.
A ready-made package (I2C `sht4x` + wiring) is `packages/mq8_tc.yaml`.

## Wiring

* MQ modules need **5 V** on `VCC` (the MQ-7/MQ-136/MQ-303A/MQ-309A also need a
  two-phase heater - drive those with an external circuit or an ESPHome
  `output`, the component only warns about it during configuration).
* The `AO` output can swing up to `VCC` (5 V) while the ESP32 ADC saturates
  around **3.1 V** at 12 dB attenuation. Use a divider (e.g. 10k / 20k) and set
  `voltage_multiplier` to the inverse of the divider ratio (`1.5` for 2/3).
* Use an ADC1 pin (ESP32: GPIO32-39); ADC2 is unavailable while WiFi is active.
* The load resistor `RL` of cheap breakouts is often **1 kOhm** instead of
  10 kOhm - measure it and set `rl:` accordingly, otherwise `RS` (and therefore
  the PPM value) is scaled wrongly.
* Install the sensor **high up**: hydrogen accumulates at the top of the room.

## Hydrogen thresholds (this project)

Reference values from `docs/h2_thresholds.md`
(H2 LEL = 4 % vol = 40 000 ppm):

| H2 concentration | Meaning |
|---|---|
| 100 - 300 ppm | trace / cell problem detection |
| 4 000 ppm | 10 % LEL - early warning |
| 10 000 ppm | 25 % LEL - pre-alarm (upper range of the MQ-8, NFPA 855 design target) |
| 20 000 ppm | 50 % LEL - immediate action (ventilate, stop charging) |

The MQ-8 saturates around 10 000 ppm, so it is a *leak detector* and not an
explosive-range instrument; the MiCS-5524 (ultra sensitive, ≤ 1 000 ppm range)
is the better early-warning device.

## Supported sensors and built-in curves

| Type | Method | RS/R0 air | Default gas | Gases with built-in coefficients |
|---|---|---|---|---|
| MQ-2 | exponential | 9.83 | LPG | LPG, H2, CH4, CO, ALCOHOL, PROPANE, SMOKE |
| MQ-3 | exponential | 60 | ALCOHOL | LPG, CH4, CO, ALCOHOL, BENZENE, HEXANE |
| MQ-4 | exponential | 4.4 | CH4 | LPG, CH4, CO, ALCOHOL, SMOKE |
| MQ-5 | exponential | 6.5 | LPG | LPG, CH4, CO, H2, ALCOHOL |
| MQ-6 | exponential | 10 | LPG | LPG, H2, CH4, CO, ALCOHOL |
| MQ-7 | exponential | 27.5 | CO | CO, H2, LPG, CH4, ALCOHOL |
| **MQ-8** | **exponential** | **70** | **H2** | **H2**, LPG, CH4, CO, ALCOHOL |
| MQ-9 | exponential | 9.6 | LPG | LPG, CH4, CO |
| MQ-131 | linear | 1 | O3 | O3 |
| MQ-135 | linear | 1 | NH3 | NH3, H2, TOLUENE |
| MQ-137 | linear | 1 | NH3 | NH3 |
| MQ-138 | linear | 1 | TOLUENE | TOLUENE, ALCOHOL, ACETONE |
| MQ-136, MQ-214, MQ-303A, MQ-309A, `CUSTOM` | – | – | – | none - `a:`/`b:` are required |

Provenance of the numbers (`coefficients.py` has the details):
`a`/`b`/method/clean-air ratio come from the `sensorConfigData.h` of the
SolderedElectronics MQ library (which carries the MQUnifiedsensor curves) and
were cross-checked against the per-sensor tables printed in the
`examples/` of miguel5612/MQSensorsLib (MQ-4 CH4 `1012.7 / -2.786`,
MQ-8 H2 `976.97 / -0.688`, MQ-3 alcohol `0.3934 / -1.504`, …).

### Alternative datasets (`curve:`)

`curve: mqdatascience` replaces the coefficients, the regression method and (for
MQ-8 H2) the whole dataset with the values published by MQDataScience - useful to
A/B compare both on the same hardware:

| RS/R0 | `curve: standard` (default) | `curve: mqdatascience` | Δ |
|---|---|---|---|
| 70 (clean air) | 52.5 ppm | 46.7 ppm | -11.1 % |
| 10 | 200.4 ppm | 178.8 ppm | -10.8 % |
| 1 | 977.0 ppm | 875.4 ppm | -10.4 % |

The slope is the same (0.688 vs 0.68994): their dataset is the same datasheet fit
anchored ≈ 11 % lower, not a more accurate one - which is why `standard` stays
the default. `curve:` cannot be combined with `a:`/`b:`/`regression_method:` (the
dataset defines them); the regression method `inverse` (`ppm = (ratio/a)^(1/b)`,
their `inverseYaxb()`) is available for hand-written coefficients as well.
Background: `docs/mqdatascience_comparison.md`.

### Deviation from `MQUnifiedsensor::readSensorR0Rs()`

The ESP-IDF port computes the ratio **inverted** (`R0 / RS`, see the comment
*"INVERTED for MQ-131 issue 28"*), while the published `a`/`b` values are
fitted against `RS / R0`. With the inverted ratio, an MQ-8 in clean air would
report ≈ 18 000 ppm instead of ≈ 50 ppm. This component therefore uses
`RS / R0` by default (`ratio_mode: rs_r0`); set `ratio_mode: r0_rs` if you have
your own coefficients fitted to the port's convention.

## Behaviour

* `update_interval` (default 60 s) triggers one reading: `samples` AO voltages
  are averaged (with `sample_interval` between them, like the library's
  `retries`/`retry_interval`), then `RS`, the ratio and the PPM are computed.
* Nothing is published (state stays `unknown`) during the warm-up window, while
  a calibration is pending/running, or while `R0` is unknown.
* A reading whose AO voltage is ≤ 10 mV (unplugged/shorted/open circuit) is
  logged as a warning and published as `unknown` instead of `0 ppm`, so a
  broken sensor cannot look like clean air.
* Out-of-range results (overflow, `FLT_MAX`) are clamped to `max_ppm`; with
  `correction_clamp: scaled` the ceiling follows the correction factor.
* With `correction_mode: mqdatascience` the ratio is divided by the T/RH
  correction before the regression; `ratio_sensor` still publishes the measured
  RS/R0, `correction_sensor` the applied factor, and the debug log shows
  `V=… RS=… ratio=… (correction=…) -> … ppm`.
* Configuration problems (unknown `sensor_type`, gas without a curve, missing
  `a`/`b`, `max_ppm <= min_ppm`, both `r0:` and `calibration:`) are reported at
  compile time; missing calibration and heater notes are logged as warnings.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `R0 unknown` / state stays `unknown` | no `r0:` and no `calibration:` configured, or the calibration has not run yet |
| `analog output reads 0.0000 V` warning | AO not connected, module not powered from 5 V, or the divider is wrong |
| PPM far too low / flat | `rl:` does not match the module's real load resistor, or the divider ratio is not compensated with `voltage_multiplier` |
| PPM pinned at `max_ppm` | wrong `a`/`b` for the selected gas, or `ratio_mode` mismatch |
| Values drift over weeks | MQ sensors drift; re-run `request_calibration()` in clean air |
| `correction_sensor` stays at 1.0000 | the T/RH sensor has no state yet (check the one-time warning in the log), or `correction_mode` is `none` |

## Tests

The measurement math lives in `mq_math.h` (no ESPHome/ESP-IDF dependency) and is
covered by a host test:

```bash
cd esphome/tests
g++ -std=c++17 -O2 -I ../components/MQ_gas_sensors mq_math_test.cpp -o mq_math_test
./mq_math_test
```

Validation helpers (no hardware needed):

```bash
esphome config config.yaml                # validate the project configuration
esphome compile config.yaml               # full ESP-IDF build
esphome config tests/test_no_id.yaml      # sensor without `id:`, `pin:`, fixed r0
esphome config tests/test_tc.yaml         # T/RH correction + `curve: mqdatascience`
python tests/inspect_config.py tests/test_no_id.yaml   # show the resolved id
```

## Documentation

Project documentation (hardware, thresholds, curve provenance, comparison with
MQDataScience) lives in [`docs/`](../../../docs/README.md) at the repository root.

## Credits

* `MQUnifiedsensor` / `MQSensorsLib` - Miguel A. Califa U., Yersson R. Carrillo
  A., Ghiordy F. Contreras C., Andres A. Martinez, Juan A. Rodriguez,
  Mario A. Rodriguez O. (MIT).
* ESP-IDF port (`MQSensorLIB`, `espidf_adc_helper.h`) and the ratio discussion -
  Carlos Delfino.
* Temperature/humidity correction model, alternative MQ-8 H2 dataset and the
  reference tables - `abcdaaaaaaaaa` / `MQDataScience` (`MQSpaceData` v6.0.0,
  MIT; `src/Correction.cpp`, `src/SensorDefinitions.cpp`).
* Curve table (`sensorConfigData.h`) - SolderedElectronics (GPL-3.0 data
  reference; the values themselves are the datasheet/miguel5612 fits).



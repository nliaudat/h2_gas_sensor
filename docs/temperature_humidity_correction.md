# Temperature/humidity compensation (optional)

The MQ-8 resistance depends on the ambient temperature and humidity, so the same
hydrogen concentration produces a different `RS/R0` in a cold dry cellar than in
a warm humid one. This component can compensate for that with the model published
by [MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience)
(`src/Correction.cpp`). On the MQ-8 it is switched on by linking the two ambient
measurements (`temperature:`/`humidity:`), see below.

The compensation is **selected automatically as soon as both ambient
measurements are linked**: write `temperature:` and `humidity:` on the MQ-8 entry
and the `mqdatascience` model runs - a separate `correction_mode:` is not
needed. The rules are:

| Configuration | Result |
|---|---|
| `temperature:` **and** `humidity:` linked | compensation **on** (`mqdatascience`), with an `INFO` in the log |
| both links, `correction_mode: none` | valid, published **uncorrected** (explicit opt-out, `INFO`) |
| only one of the two links | **rejected** at `esphome config` time - a half-wired pair is always a mistake |
| explicit `correction_mode:` without the links | **rejected** |
| `correction_mode:` for a type without constants | **rejected** (list of supported types in the message) |
| both links on a type without constants | valid, links **ignored** with a `WARNING` |

## The model

```
RH'  = clamp(RH, 33, 85)          %      RH is clamped to the fitted range
T'   = clamp(T, -10, 50)         degC    temperature clamp
a    = a33 + (RH' - 33) / (85 - 33) × (a85 - a33)     (same for b and c)
correction = a + c × exp(b × T')
ratio_eff  = ratio / correction   ->  PPM = 976.97 × ratio_eff^(−0.688)
```

Because `b` is negative, dividing the ratio by a `correction < 1` *lowers* the
reported value (and `> 1` raises it). Algebraically this is exactly
MQDataScience's `PPM = (ratio / (a × correction))^(1/b)` with their coefficients:
the ppm is scaled by `correction^0.68994`.

## MQ-8 constants (`Correction.cpp`)

| RH 33 % | RH 85 % |
|---|---|
| a = 0.8559, b = −0.0611, c = 0.1673 | a = 0.8201, b = −0.0606, c = 0.1492 |

Other types with the same one-segment model are also available
(`coefficients.py` → `CORRECTION_COEFFICIENTS`): MQ-2, MQ-3, MQ-4, MQ-5, MQ-6,
MQ-7, MQ-135, MQ-136, MQ-137, MQ-138, MQ-214. MQ-9 and MQ-131 use a two-segment
variant (RH 30/60/85 %) and are rejected at compile time.

## Effect on the MQ-8 reading

| RH % | T °C | correction | ppm × |
|---|---|---|---|
| 40 | 20 | 0.8997 | 0.930 |
| 60 | 25 | 0.8718 | 0.910 |
| 33 | 10 | 0.9467 | 0.963 |
| 85 | 35 | 0.8380 | 0.885 |
| 50 | −10 | 1.1410 | 1.095 |
| 50 | 50 | 0.8519 | 0.895 |

| Range | correction | ppm × |
|---|---|---|
| realistic indoor (RH 30 – 70 %, 15 – 30 °C) | 0.8554 … 0.9253 | **0.898 … 0.948** |
| full fitted envelope (RH 33 – 85 %, −10 … 50 °C) | 0.8273 … 1.1641 | 0.877 … 1.111 |

Two consequences worth knowing:

1. Inside a normal room the compensation changes the reading by only **±3 %** —
   well below the ±30 % sensor-to-sensor spread of an MQ-8. It is a refinement
   for long-term stability, not an accuracy fix.
2. It is *not* neutral at reference conditions: with the compensation enabled, a
   perfectly calibrated sensor in clean air reports ≈ **48.8 ppm instead of
   52.5 ppm** at RH 40 %/20 °C (the model's own clean-air anchor). Enabling it
   shifts the whole baseline by roughly −7 %; that is the model's behaviour, not
   a calibration error.

## Configuration

```yaml
sensor:
  - platform: sht4x
    i2c_id: bus_a
    address: 0x44
    temperature: { id: air_temperature, name: "Ambient temperature" }
    humidity:    { id: air_humidity,    name: "Ambient humidity" }

  - platform: mq_gas_sensors
    id: mq8
    sensor_type: MQ-8
    gas: H2
    pin: GPIO34
    temperature: air_temperature     # linking both switches the correction on
    humidity: air_humidity
    # correction_mode: none          # only to opt out again
    correction_clamp: absolute       # default: keep max_ppm as the ceiling
    correction_sensor: mq8_correction  # optional diagnostic entity
```

The shipped package
[`../esphome/packages/mq8.yaml`](../esphome/packages/mq8.yaml) keeps the links
commented, because their ids only exist once a T/RH sensor is wired - a package
cannot add keys by itself. The idiomatic way to connect a sensor that lives in
your own file/package is **`id: !extend mq8`**, which merges the links into the
package entry without editing it (the same mechanism
[`../esphome/tests/test_mq8_tc_package.yaml`](../esphome/tests/test_mq8_tc_package.yaml)
verifies):

```yaml
sensor:
  - platform: dht
    pin: GPIO32
    model: DHT22
    temperature: { id: air_temperature, name: "Ambient temperature" }
    humidity:    { id: air_humidity,    name: "Ambient humidity" }

  - platform: mq_gas_sensors
    id: !extend mq8                  # the entry of packages/mq8.yaml
    temperature: air_temperature
    humidity: air_humidity
    correction_sensor: mq8_correction  # optional diagnostic (the factor applied)
```

Both variants define the same two keys, so any platform works (SHT4x, DHT22,
BME280, a sensor imported from Home Assistant, ...): only the ids are used,
wherever they are defined. The shipped `packages/mq8.yaml` enables the
`MQ-8 T-RH correction` entity (`${name}_mq8_correction`) and
`packages/dht22.yaml` links it with `correction_sensor:`, so the applied factor
is visible in Home Assistant (1.0000 = uncorrected, either because no T/RH sensor
is linked or because `correction_mode: none` was written). The package also
carries commented `sht4x` and `dht` examples that define
`${name}_mq8_temperature` / `${name}_mq8_humidity`, for the "uncomment and go"
setup. The DHT11 resolves only 1 °C / 1 % RH over 0 - 50 °C / 20 - 90 % RH, which
is about the ±3 % the compensation shifts indoors - prefer the SHT4x (or a DHT22,
±0.5 °C / ±2 % RH) when the ambient reading itself matters.

| Key | Default | Description |
|---|---|---|
| `temperature` | – | `id` of a temperature sensor (°C). Linking it **and** `humidity` switches the compensation on. |
| `humidity` | – | `id` of a relative humidity sensor (%). |
| `correction_mode` | `mqdatascience` when both links are set, otherwise `none` | `none` or `mqdatascience` - only needed to opt out or to be explicit. |
| `correction_clamp` | `absolute` | `absolute` clips to `max_ppm`; `scaled` clips to `max_ppm × correction` (MQDataScience's own behaviour). |
| `correction_sensor` | – | Optional diagnostic entity receiving the applied factor (1.0000 = uncorrected). |

## Behaviour and safety rules

* **Fail-open**: if the T/RH sensor is missing, has no state yet (right after
  boot) or reports `NaN`, the reading is published **uncorrected** (factor
  1.0000) with a one-time warning in the log. An auxiliary sensor failure never
  suppresses or zeroes the gas measurement.
* **Clamp**: `absolute` keeps the alarm ceiling at `max_ppm` (10 000 ppm =
  25 % LEL). With `scaled`, the ceiling would fall to ≈ 8 900 ppm in warm humid
  air — for a safety limit the absolute clamp is the right choice (see
  [`h2_thresholds.md`](h2_thresholds.md)).
* **Diagnostics**: `ratio_sensor` publishes the *measured* RS/R0 (uncorrected),
  `correction_sensor` the factor that was applied, and the debug log prints
  `V=… RS=… ratio=… (correction=…) -> … ppm`.
* **Calibration is uncorrected**, like MQDataScience: `R0` is computed from the
  raw clean-air `RS`, the compensation is applied to every later reading.
* The resolution is checked at `esphome config` time: an explicit
  `correction_mode` without both links, a mode for a type without constants, and a
  *partial* link (`temperature:` without `humidity:`) fail with an explanatory
  message. Both links on a type without constants stay valid and warn that they
  are ignored - that type cannot compensate.

## Verification

`esphome/tests/mq_math_test.cpp` asserts the model against
`Correction.cpp` for the MQ-8 constants, including every number in the tables
above (e.g. RH 40 %/20 °C → 0.8997 and ppm × 0.9297), the RH/T clamping, the
`NaN → 1.0` fallback and the no-op behaviour of a factor of 1.0.

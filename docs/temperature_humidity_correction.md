# Temperature/humidity compensation (optional)

The MQ-8 resistance depends on the ambient temperature and humidity, so the same
hydrogen concentration produces a different `RS/R0` in a cold dry cellar than in
a warm humid one. This component can compensate for that with the model published
by [MQDataScience](https://github.com/abcdaaaaaaaaa/MQDataScience)
(`src/Correction.cpp`), switched on per sensor with `correction_mode`.

Default: **off** (`correction_mode: none`) — the compensation needs a T/RH sensor
and slightly lowers the reported ppm values, so it is opt-in.

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

  - platform: MQ_gas_sensors
    id: mq8
    sensor_type: MQ-8
    gas: H2
    pin: GPIO34
    temperature: air_temperature     # required with the correction
    humidity: air_humidity           # required with the correction
    correction_mode: mqdatascience
    correction_clamp: absolute       # default: keep max_ppm as the ceiling
    correction_sensor: mq8_correction  # optional diagnostic entity
```

Ready-made version: [`../esphome/packages/mq8_tc.yaml`](../esphome/packages/mq8_tc.yaml)
(include it **instead of** `packages/mq8.yaml`).

| Key | Default | Description |
|---|---|---|
| `temperature` | – | `id` of a temperature sensor (°C). Required for `correction_mode`. |
| `humidity` | – | `id` of a relative humidity sensor (%). Required for `correction_mode`. |
| `correction_mode` | `none` | `none` or `mqdatascience`. |
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
* Enabling the compensation for a type without constants, or enabling it without
  `temperature:`/`humidity:`, fails at `esphome config` time with an explanatory
  message.

## Verification

`esphome/tests/mq_math_test.cpp` asserts the model against
`Correction.cpp` for the MQ-8 constants, including every number in the tables
above (e.g. RH 40 %/20 °C → 0.8997 and ppm × 0.9297), the RH/T clamping, the
`NaN → 1.0` fallback and the no-op behaviour of a factor of 1.0.

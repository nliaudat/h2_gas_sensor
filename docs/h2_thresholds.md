# Hydrogen thresholds for a battery room

Reference values used by this project. The LEL of hydrogen (4 % vol = 40 000 ppm)
is physical data; the 25 % LEL pre-alarm follows the NFPA 855 design target for
stationary battery installations. See [`README.md`](README.md#sources) for the
source list.

## Conversion

| Volume | ppm | % of the LEL |
|---|---|---|
| 4 % vol | 40 000 ppm | 100 % (LEL, lower explosive limit) |
| 2 % vol | 20 000 ppm | 50 % |
| 1 % vol | 10 000 ppm | 25 % |
| 0.4 % vol | 4 000 ppm | 10 % |
| – | 100 – 300 ppm | 0.25 – 0.75 % (trace) |

`ppm = % of LEL × 400` (for H₂, 1 % of the LEL = 400 ppm), and
`% vol = ppm / 10 000`.

## Thresholds

| H₂ concentration | Meaning | Recommended action |
|---|---|---|
| 100 – 300 ppm | trace / normal end-of-charge gassing of NiFe cells | log only, watch the trend |
| 4 000 ppm | 10 % LEL — early warning | notification, check ventilation |
| 10 000 ppm | 25 % LEL — **pre-alarm** (NFPA 855 design target, end of the MQ-8 range) | ventilate, stop charging |
| 20 000 ppm | 50 % LEL | immediate intervention (forced ventilation, disconnect) |

## What this sensor can and cannot do

* The MQ-8 range is 100 – 10 000 ppm, and the firmware clamps at
  `max_ppm: 10000`. It is therefore a **leak detector with a pre-alarm**, not an
  explosive-range instrument: the real LEL (40 000 ppm) is beyond both sensors
  discussed for this project.
* For the trace range (100 – 1 000 ppm) the MiCS-5524 is the more sensitive
  device; the MQ-8 covers the wide range up to the regulatory 25 % LEL ceiling.
  Both are available as packages: `packages/mics5524.yaml` (trace) and
  `packages/mq8.yaml` / `packages/mq8_sht4x.yaml` / `packages/mq8_dht11.yaml`
  (pre-alarm, the last two with the T/RH correction) - see
  [`mics5524_guide.md`](mics5524_guide.md) for the wiring and the accuracy
  caveats of the MiCS.
* A nickel-iron battery produces hydrogen as soon as it approaches full charge
  and keeps producing if charging continues — the gas release is part of normal
  operation, not only a fault. A slow upward trend is expected around the end of
  a charge cycle.
* Hydrogen is very light: it accumulates at the **top** of the room or of the
  enclosure. Mount the sensor high up, not next to the cells.

## Where to alert

Alerting belongs in Home Assistant (or an `on_value` automation), not in the
component. The value published by the component is the one to use:

* keep the alarm thresholds on the **absolute** ppm value
  (`correction_clamp: absolute`, the default) - with the T/RH compensation
  enabled, `scaled` would lower the 10 000 ppm ceiling to ≈ 8 900 ppm in warm,
  humid air (see
  [`temperature_humidity_correction.md`](temperature_humidity_correction.md));
* if the T/RH compensation is enabled, alert on the corrected value (it is the
  best estimate) but keep in mind that it is ~7 % below the uncorrected one in
  typical indoor conditions.

```yaml
# packages/mq8*.yaml
    on_value:
      - if:
          condition:
            sensor.in_range: { above: 10000 }
          then:
            - logger.log: "H2 pre-alarm: 25 % LEL"
```

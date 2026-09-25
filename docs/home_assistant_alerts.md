# Home Assistant - entities, thresholds, alerts

What the device publishes, how to read it and paste-ready alerting. The threshold
reasoning is in [`h2_thresholds.md`](h2_thresholds.md), the sensors themselves in
[`mq8_sensor_guide.md`](mq8_sensor_guide.md) and
[`mics5524_guide.md`](mics5524_guide.md).

## Entities

| Entity (suffix of `friendly_name`) | Unit | Kind | Package |
|---|---|---|---|
| `H2 (MQ-8)` | ppm | measurement | `packages/mq8.yaml` |
| `MQ-8 AO voltage` | V | diagnostic | `packages/mq8.yaml` |
| `MQ-8 RS-R0 ratio` | - | diagnostic | `packages/mq8.yaml` |
| `MQ-8 RS` | kΩ | diagnostic | `packages/mq8.yaml` |
| `MQ-8 T/RH correction` | - | diagnostic | `packages/mq8.yaml`, compensation enabled |
| `ambient temperature` | °C | measurement | `packages/mq8.yaml`, T/RH sensor enabled |
| `ambient humidity` | % | measurement | `packages/mq8.yaml`, T/RH sensor enabled |
| `H2 trace (MiCS-5524)` | ppm | measurement | `packages/mics5524.yaml` |
| `CO (MiCS-5524)` | ppm | measurement | `packages/mics5524.yaml` |
| `NH3 (MiCS-5524)` | ppm | measurement | `packages/mics5524.yaml` |
| `C2H5OH (MiCS-5524)` | ppm | measurement | `packages/mics5524.yaml` |
| `CH4 (MiCS-5524)` | ppm | measurement | `packages/mics5524.yaml` |
| `MiCS-5524 AO voltage` | V | diagnostic | `packages/mics5524.yaml` |
| `MiCS-5524 AO (scaled)` | V | diagnostic | `packages/mics5524.yaml` |
| `MiCS-5524 ratio` | - | diagnostic | `packages/mics5524.yaml` |
| `MQ-8 logs` | text | diagnostic | `packages/mq8.yaml` |
| `MQ-8 recalibrate` | - | button (config) | `packages/mq8.yaml` |
| `MiCS-5524 logs` | text | diagnostic | `packages/mics5524.yaml` |
| `MiCS-5524 recalibrate` | - | button (config) | `packages/mics5524.yaml` |
| `WiFi Signal` | dBm | diagnostic | `packages/sensors_others.yaml` |
| `restart` | - | switch | `packages/switch.yaml` |

Diagnostic entities are categorised as such, so they stay out of the default
dashboard. With `friendly_name: "H2 sensor board"` the alarm entity is
`sensor.h2_sensor_board_h2_mq_8` - keep `name` / `friendly_name` stable once you
have automations, otherwise every entity id changes.

The two `logs` text sensors mirror the component log (the per-update chain, the
calibration messages and the warnings) - the same text `esphome logs` prints, so
the measurement chain can be read from Home Assistant without touching
`logger.logs`.  Each state replaces the previous one; exclude them from the
recorder if the 30 s history is not wanted.

The two `recalibrate` buttons start a clean-air calibration (`request_calibration()`).
**Only press them in clean air**: the measured reference is written to flash and
used from then on.  A request is deferred until `warmup_time` has elapsed and the
value stays `unknown` while the calibration is pending or running.

Each sensor updates every **30 s** (four averaged ADC samples). The
temperature/humidity-corrected value is published on the same `H2 (MQ-8)`
entity; the diagnostic `MQ-8 RS-R0 ratio` always stays uncorrected.

## Thresholds

| H₂ concentration | Meaning | Recommended action |
|---|---|---|
| 100 - 300 ppm | trace, normal end-of-charge gassing | log, watch the trend |
| 4 000 ppm | 10 % of the LEL - early warning | notification, check ventilation |
| 10 000 ppm | 25 % of the LEL - pre-alarm, end of the MQ-8 range | ventilate, stop charging |
| 20 000 ppm | 50 % of the LEL | immediate intervention (forced ventilation, disconnect) |

The MQ-8 stops at 25 % of the LEL (the firmware clamps at `max_ppm: 10000`): this
is a leak detector with a pre-alarm, not an explosive-range instrument.

## Alerts

Two automations cover the interesting cases. Adjust the entity ids and the notify
service to your setup; `for:` ignores a single spike.

```yaml
# Home Assistant (automations.yaml)
- alias: "H2 pre-alarm (10 000 ppm)"
  trigger:
    # The component clamps at max_ppm (10 000 ppm) and numeric_state's `above:`
    # is exclusive, so `above: 10000` would never fire: `>=` needs a template.
    - platform: template
      value_template: "{{ states('sensor.h2_sensor_board_h2_mq_8') | float(0) >= 10000 }}"
      for: "00:02:00"
  action:
    - service: notify.mobile_app_your_phone
      data:
        title: "H2 pre-alarm"
        message: ">= 10 000 ppm (25 % LEL) - ventilate and stop charging."

- alias: "H2 trace warning (MiCS-5524, 300 ppm)"
  trigger:
    - platform: numeric_state
      entity_id: sensor.h2_sensor_board_h2_trace_mics_5524
      above: 300
      for: "00:05:00"
  action:
    - service: notify.mobile_app_your_phone
      data:
        title: "H2 trace warning"
        message: "MiCS-5524 above 300 ppm - check the trend and the ventilation."
```

The pre-alarm is a template trigger on purpose: the MQ-8 saturates at
`max_ppm: 10000`, and the `numeric_state` trigger's `above:`/`below:` are
exclusive (Home Assistant has no `above_or_equal:` key), so `above: 10000` would
never fire. The MiCS-5524 trace automation can stay on `numeric_state`, since
300 ppm is well inside its 1 000 ppm range. The ESPHome-side examples use
`sensor.in_range: { above: ... }`, which *is* inclusive (`state >= min`).

For the dashboard a history graph of `H2 (MQ-8)` plus `H2 trace (MiCS-5524)` is
enough, with the thresholds in mind; add `WiFi Signal` and the diagnostics when
something looks wrong.

## Which sensor answers which question

| Question | Sensor |
|---|---|
| "do I need to act?" | `H2 (MQ-8)` - the alarm reference (4 000 - 10 000 ppm) |
| "is something happening before the MQ-8 reacts?" | `H2 trace (MiCS-5524)` - trend in the 100 - 1 000 ppm band |
| "why is the reading what it is?" | the diagnostics: `MQ-8 AO voltage` (wiring/divider), `RS` and `RS-R0 ratio` (RL, ageing), `T/RH correction` (compensation) |

The MiCS-5524 sees five gases on a single output and cannot tell them apart: use
it for the trend, never as the only alarm source. `packages/mics5524.yaml`
publishes every gas of the vendor table - `H2 trace`, `CO`, `NH3`, `C2H5OH` and
`CH4` - but these are the *same* analog output interpreted with five different
curves, not five independent measurements: keep the alerting on `H2 trace` (and
on the MQ-8).

## Operations

* **Weekly restart** - `packages/time.yaml` restarts the board every Monday at
  06:00 (SNTP has to be synced first). The calibration is in flash and survives
  it; useful against long-run drift.
* **Watchdog and flash wear** - `packages/board.yaml` sets a 30 s task watchdog,
  240 MHz, `FREERTOS_HZ 1000` and TLS 1.3; `preferences.flash_write_interval:
  60min` keeps the calibration writes gentle.
* **Recovery paths** - `safe_mode:` (boots without the custom components after
  repeated crashes), `api: reboot_timeout: 30min`, OTA (`ota: platform: esphome`)
  and the `restart` switch.
* **Logs** - `logger:` runs at `DEBUG` with per-tag overrides (the two gas-sensor
  tags are pinned at `INFO`); `esphome logs config.yaml` (or the web log) prints
  the measurement chain - see [`troubleshooting.md`](troubleshooting.md) for how
  to read it. The same messages are mirrored into Home Assistant by the
  `MQ-8 logs` / `MiCS-5524 logs` text sensors, so the chain is readable without
  changing the logger level.
* **Re-calibration** - the `MQ-8 recalibrate` / `MiCS-5524 recalibrate` buttons
  start a clean-air calibration from Home Assistant (press them *only* in clean
  air; the value is stored in flash). The MiCS button refreshes all five gas
  entries because they share one physical sensor.
* **Firmware updates** - `esphome run config.yaml` over OTA, then press `EN` once
  so the new firmware starts.

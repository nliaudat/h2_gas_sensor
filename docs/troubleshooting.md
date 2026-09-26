# Troubleshooting

Two stages: **setup / configuration** (nothing is flashed yet) and **runtime**
(the device runs but the numbers are wrong). Per-sensor causes are collected in
[`mq8_sensor_guide.md`](mq8_sensor_guide.md) and
[`mics5524_guide.md`](mics5524_guide.md).

## Setup and configuration

| Symptom | Likely cause | What to do |
|---|---|---|
| `esphome config` fails with "... can damage the pin" | the configuration allows more than `adc_pin_max` (3.6 V) at the ADC pin - e.g. a 5 V module with `voltage_multiplier: 1.0` and no divider | add `divider: {r1: 10.0, r2: 20.0}` (5 V -> 3.33 V), switch to 10k/10k (2.5 V), or declare `adc_pin_max` when the ADC really does see the full swing (ADS1115) |
| warning "top of the range may clip" | the divider maps the sensor maximum slightly above `adc_input_max` (10k/20k -> 3.33 V vs 3.3 V recommended) | accept it - only the very top of the range reads non-linearly - or use 10k/10k |
| `Duplicate id: mq8` | `packages/mq8.yaml` is included twice (for instance under two different package names in `config.yaml`) | include the MQ-8 package exactly once |
| the configuration validates but nothing arrives in Home Assistant | missing/typo in `secrets.yaml`, or the board is on another network | try the fallback AP `<name> Fallback` (password `fallback_hotspot_password`), then the captive portal |
| upload fails: no serial device, or the port is busy | missing USB driver (CP2102/CH340), or another program holds the port | install the driver, close the serial monitor, try another cable (some are charge-only) |
| upload fails with "Failed to connect" | the board is not in bootloader mode | hold `BOOT` for 2-3 s while the connection initialises |
| build fails with a locked or corrupted build cache | a sync client or a second `esphome` process is indexing `esphome/.esphome/` | close the other process, delete `esphome/.esphome/` and rebuild |
| GPIO12 or an ADC2 pin in the config | GPIO12 is a strapping pin, ADC2 is unusable while Wi-Fi is active | move to an ADC1 pin (GPIO32-39) |
| `Duplicate id: mq8` with `packages/alarm.yaml` | the alarm package is included *before* `mq8:` in `config.yaml`, so its `!extend mq8` fragment cannot merge | keep `alarm: !include packages/alarm.yaml` after the `mq8:` line |
| `Couldn't find ID 'mq8'` at config time | `packages/alarm.yaml` is included without `packages/mq8.yaml` (it reads `id: mq8`) | include `mq8` or drop the `alarm` include |

## Runtime

| Symptom | Likely cause | What to do |
|---|---|---|
| state stays `unknown`, log says `R0 unknown` / `no air reference` | no `calibration:` block and no pinned reference, or the calibration has not run yet (delay / warm-up window) | wait for the delay or the burn-in, or pin `r0:` / `air_reference:` |
| the value stays `unknown` after pressing *recalibrate* | this is intentional: a request is deferred until `warmup_time` ends, and nothing is published while the calibration is pending or running (the reference is about to change) | watch the `MQ-8 logs` / `MiCS-5524 logs` text sensor - it reports the remaining warm-up time and the calibration result |
| the MiCS ratio sits at 0.8 - 0.9 in clean air / drifts away from 1.0 | stale clean-air reference - captured during the warm-up or before the burn-in finished, or the sensor drifted | re-calibrate once the reading is stable (24 - 48 h burn-in for a new module); the vendor model over-reports while the reference is too high |
| warning `analog output reads 0.0000 V` | `AO` not connected, module not powered from 5 V, wrong divider node, MiCS `EN` polarity | re-check the wiring and `inverted:` |
| ppm far too low, or nearly flat | `rl:` does not match the module (measure the resistor between `AO` and GND), or the divider ratio changed without updating `r1`/`r2` | set `rl:` / `divider:` to the measured values |
| ppm pinned at `max_ppm` (10 000 for the MQ-8, 1 000 for the MiCS) | the gas is outside the range, the curve does not match the gas, or the divider is wrong | check `gas:` / `curve:`, then the divider |
| readings jump around | ESP32 ADC noise - the 1 s entities jitter; polling the diagnostics slower (`mq8_diag_interval` / `mics_extra_gas_update_interval`) only records less of it | raise `samples:` (e.g. 16), add a 100 nF capacitor from the ADC pin to GND, or use an ADS1115 |
| values drift over weeks | normal metal-oxide drift | re-calibrate in clean air - see [`getting_started.md`](getting_started.md) |
| `MQ-8 T-RH correction` stuck at 1.0000 | the T/RH sensor has no state yet, the `temperature:`/`humidity:` links are commented or the compensation block is not uncommented in `packages/mq8.yaml`, a wrong I2C address, or an unreadable DHT11 data line | check the log warning, the links and pins in `packages/mq8.yaml` (`mq8_i2c_sda`/`mq8_i2c_scl`, `mq8_sht4x_address`, `mq8_dht_pin`), or the pull-up of the DHT11 |
| the MiCS-5524 reacts to something that is not hydrogen | it has a single output for CO, H2, ethanol, ammonia and methane | treat it as a trend sensor; the MQ-8 is the reference for alarms |
| the readings are plausible but the room smells of hydrogen | the sensor sits at the wrong height | hydrogen accumulates at the top: mount it at the highest point of the room or enclosure |
| the buzzer stays quiet although the ppm is high | by design above the danger threshold: from 10 000 ppm (25 % of the LEL, the MQ-8 ceiling) the local annunciator goes visual-only and silences the buzzer | look at the red LEDs and the Home Assistant automations; move `alarm_danger_ppm` only deliberately (see [`local_alarm.md`](local_alarm.md)) |
| no sound at all from the buzzer, while the LEDs work | an "active" buzzer (with its own oscillator) instead of a passive piezo, a piezo behind a transistor that is not driven, or `gain:` at 0 % | use a **passive** piezo on `alarm_buzzer_pin` and press `alarm test` |
| the `alarm LEDs` show blue for minutes after a reboot | the MQ-8 publishes `unknown` until its first clean-air calibration finished (`calibration.delay` + `samples`), and the annunciator shows that as "no reading" | wait for the delay; if it stays blue, the calibration failed - read the `MQ-8 logs` text sensor |
| the alarm LEDs flicker or show a random colour once | 3.3 V data into a 5 V SK6812 (marginal, needs ~3.5 V), or no pull-down on the data line | add a 74AHCT125 / a series diode in the strip's 5 V feed, plus the 10 kΩ pull-down - see [`local_alarm.md`](local_alarm.md) |
| `history records` stays 0 / the log says `no valid time yet` | the clock has no SNTP sync (offline board), so the history refuses to write rows with a boot counter as their timestamp | expected offline; the history starts with the first sync, or set `require_time: false` (see [`data_logging.md`](data_logging.md)) |
| `history` reports rows but Home Assistant has no history for the window | the history is a **copy on the device**, not a recorder backend: read it with the `history dump` button or the aggregates | see [`data_logging.md`](data_logging.md) for the export paths |
| `no data/littlefs partition labelled 'littlefs'` after an OTA update | the partition table changed and OTA cannot move partitions | flash once over USB (`esphome run config.yaml`) |
| `history write errors` above 0 | a write or `tsdb_sync_h()` failed (flash or filesystem trouble) | check `history free`; see [`data_logging.md`](data_logging.md) |
| `history records` stays 0 after an update that changed the logged columns; the log says `opening as N columns failed on an existing file` | the stored file was written with a different column count - esp_tsdb refuses that schema (it would mislabel every value) | expected once: the package sets `recreate_on_schema_change: true`, so the component deletes the old file and starts an empty one (`history recreated`) - the previous history is gone by design; see [`data_logging.md`](data_logging.md) |

## Reading the log

`esphome logs config.yaml` (or the web log) mixes two sources. **The device log**
is what the firmware prints: with the shipped `logger:` configuration
([`../esphome/config.yaml`](../esphome/config.yaml), both gas-sensor tags pinned
at `INFO`) that is one line per update with the published value - the "normal
mode":

```
[I][mq_gas_sensors]: 'MQ-8 H2': 93.7 ppm
[I][mics_5524_gas_sensor]: 'H2': 0.0 ppm
```

The whole measurement chain stays at `DEBUG` and appears as soon as
`mq_gas_sensors` / `mics_5524_gas_sensor` are set back to `DEBUG` under
`logger.logs` (`log_ppm:` in the two packages switches the value line above -
see the logger comment in `config.yaml`).  Illustrative values:

```
[D][mq_gas_sensors]: 'MQ-8 H2': V=1.234 V, RS=12.345 kOhm, ratio=42.100 (correction=0.9987) -> 74.5 ppm
[D][mics_5524_gas_sensor]: 'H2': V_AO=1.234 V, x=3.7660, ratio=0.9980 (dfrobot) -> 0.0 ppm
[D][mics_5524_gas_sensor]: 'CO': V_AO=1.234 V, RS=30.519 kOhm, ratio=0.8720 (datasheet) -> 7.3 ppm
```

**The synthesised state lines** are the second source: the log *client*
(`esphome logs`, the dashboard) subscribes to every entity state and prints one
line per change, tagged `[S]`:

```
[S][sensor]: 'H2 sensor board MQ-8 RS' >> 86.829 kOhm
```

They do not come from the firmware (no logger tag or level can filter them) and
they are what makes a 1 Hz device look noisy.  Switch them off when only the
values matter and the log shows exactly the device log above:

```bash
esphome logs --no-states config.yaml     # or: set ESPHOME_LOG_STATES=0
```

* `V` (MQ) / `V_AO` (MiCS) is the scaled voltage at the pin (after
  `voltage_multiplier`) - use it to verify the divider ratio;
* the MiCS prints `x = VCC - V_AO` for the vendor model and `RS` for the
  datasheet model - each line shows the quantity its model uses;
* `ratio` is RS/R0 for the MQ-8 and for the MiCS datasheet model, or the vendor
  ratio for `conversion: dfrobot` (≈ 1.0 in clean air, dropping when a reducing
  gas arrives);
* `correction` is the T/RH factor (1.0000 = none);
* the ppm value at the end is what the sensor entity publishes.

Both packages also mirror the chain - and the calibration messages and warnings
- into the `MQ-8 logs` / `MiCS-5524 logs` text sensors (`log_sensor:`), so the
same chain can be read from Home Assistant without changing `logger.logs`.  The
per-update *value* line is console-only: a text state per second would flood the
recorder.

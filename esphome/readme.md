# H2 sensor board - ESPHome firmware

ESPHome firmware for a battery-room **hydrogen monitor**: an ESP32 reads an MQ-8
(and optionally a MiCS-5524) gas sensor, converts the analog output into a ppm
value and publishes it to Home Assistant.

| | |
|---|---|
| Board | ESP32 devkit (`az-delivery-devkit-v4` by default), ESP-IDF framework |
| Sensors | MQ-8 (4 000 - 10 000 ppm band, pre-alarm) + optional MiCS-5524 (100 - 1 000 ppm trace band) |
| Optional | Ambient temperature/humidity: `packages/dht22.yaml` (1-wire DHT22, shipped) or any other sensor (SHT4x on I2C, ...) linked by id - it selects the MQDataScience compensation of the MQ-8 ratio |
| Components | `components/mq_gas_sensors/` (MQ-2 ... MQ-309A) and `components/mics_5524_gas_sensor/` (MiCS-5524) |

This file is the **firmware reference**: which package does what, where every
setting lives and how the board behaves in operation. For the first install read
[`../docs/getting_started.md`](../docs/getting_started.md); for the entities,
thresholds and automations see
[`../docs/home_assistant_alerts.md`](../docs/home_assistant_alerts.md).

## Packages

```yaml
packages:
  wifi: !include packages/wifi.yaml
  board: !include packages/board.yaml
  time: !include packages/time.yaml
  sensors_others: !include packages/sensors_others.yaml
  switch: !include packages/switch.yaml
  mq8: !include packages/mq8.yaml          # ADC + gas sensor; T/RH blocks commented inside
  dht22: !include packages/dht22.yaml      # ambient T/RH + the links that switch the compensation on
  alarm: !include packages/alarm.yaml      # local pre-alarm: buzzer + 2 x SK6812 (optional hardware)
  # mics5524: !include packages/mics5524.yaml   # additive, optional hardware
```

| Package | Sensor | Notes |
|---|---|---|
| `mq8.yaml` | MQ-8 | ADC + gas sensor; the ambient T/RH sensor blocks and the comment-only compensation keys are inside |
| `dht22.yaml` | DHT22 (ambient T/RH) | links `temperature:`/`humidity:`/`correction_sensor:` into `id: mq8` with `!extend`, which selects the compensation and feeds the `MQ-8 T-RH correction` entity |
| `mics5524.yaml` | MiCS-5524 | additive (`id: mics`), trace band, own calibration |
| `alarm.yaml` | Buzzer + 2 x SK6812 | local pre-alarm (buzzer + status LEDs); merges its bands into `id: mq8` with `!extend`, so it must stay after `mq8.yaml` |

`mq8.yaml` is the only MQ-8 package: the ambient sensors are **linked by id**
(`temperature:`/`humidity:` in the `mq_gas_sensors` entry), so there is no
per-sensor variant and nothing to keep in sync. Linking **both** ids selects the
MQDataScience compensation automatically; `correction_mode: none` in the linking
fragment is the explicit opt-out and a half-wired pair fails at config time. Two
ways to link, both documented in
[`../docs/temperature_humidity_correction.md`](../docs/temperature_humidity_correction.md):

* `packages/dht22.yaml` - the shipped 1-wire example, which extends the MQ-8 entry
  from the outside (`id: !extend mq8`), so `mq8.yaml` stays untouched;
* uncomment the SHT4x/DHT block inside `packages/mq8.yaml` together with the two
  `temperature:`/`humidity:` keys (the ids `${name}_mq8_temperature` /
  `${name}_mq8_humidity` are defined by those examples).

A DHT11 (1 °C / 1 % RH resolution) keeps the compensation within its own error, a
DHT22 (±0.5 °C / ±2 % RH) is better, an SHT4x better still.
`mics5524.yaml` can be added on top and keeps the MQ-8 defaults untouched.

## User-editable files

| File | Purpose | Usually edited? |
|---|---|---|
| `secrets.yaml` | WiFi credentials + fallback AP password (create it - see `packages/wifi.yaml` for the keys) | **Always** |
| `config.yaml` | `substitutions:` (`name`, `friendly_name`, `board_type`, `TZ`) and the package includes | **Always** |
| `packages/mq8.yaml` | MQ-8 pins/divider (`mq8_pin`, `mq8_divider_r1/r2`, `mq8_rl`) plus the commented T/RH sensor (`mq8_i2c_*`, `mq8_sht4x_address`, `mq8_dht_pin`, `mq8_dht_model`) and compensation blocks | Yes |
| `packages/dht22.yaml` | DHT22 pin/model (`dht22_pin`, `dht22_model`) + the `!extend mq8` fragment that links `temperature:`/`humidity:` (drop the package include when no T/RH sensor is wired) | Only with a DHT22 |
| `packages/mics5524.yaml` | MiCS-5524 pins/divider/EN plus one gas entity per vendor curve (H2, CO, NH3, C2H5OH, CH4 - optional hardware) | Only with a MiCS-5524 |
| `packages/alarm.yaml` | Buzzer/LED pins, chipset and channel order (`alarm_buzzer_pin`, `alarm_led_*`), the two thresholds (`alarm_early_ppm`, `alarm_danger_ppm`), the beep interval and the melody | Only with a buzzer / LED strip |
| `packages/wifi.yaml` | WiFi networks (`!secret` references) | Almost always |
| `packages/board.yaml` | ESP-IDF, watchdog/sdkconfig, API/OTA, safe mode | Rarely |
| `packages/time.yaml` | SNTP + the weekly 06:00 restart | Rarely |
| `packages/switch.yaml`, `packages/sensors_others.yaml` | Restart switch, WiFi signal diagnostics | Rarely |

## Wiring in one screen

```
MQ-8 / MiCS-5524 at 5 V
   VCC -> 5V (module GND to the ESP32 GND)
   AO  -> [ 10k ] --> ADC1 pin (GPIO32-39)
                  |
                [ 20k ]
                  |
                 GND            ->  divider: {r1: 10.0, r2: 20.0}  (multiplier 1.5)
   EN (MiCS-5524 only) -> GPIO4, `inverted: true` (the module is enabled by LOW)
```

* The analog output can reach 5 V while the **ESP32 ADC pins are not 5 V
  tolerant** (absolute maximum VDD + 0.3 V = 3.6 V): always keep the divider.
  Never wire the sensor output straight to a GPIO.
* The recommended 10k/20k divider maps 5 V to 3.33 V, a hair above the ADC's
  3.3 V recommended maximum - the packages therefore declare
  `adc_input_max: 3.33`, and the component **rejects** any configuration whose
  divider could exceed `adc_pin_max` (3.6 V) at compile time. Use 10k/10k
  (5 V -> 2.5 V) or an ADS1115 (0-5 V input, `voltage_multiplier: 1.0`,
  `adc_input_max`/`adc_pin_max: 6.144`) instead.
* Use an ADC1 pin (GPIO32-39); ADC2 is unusable while WiFi is active, and GPIO12
  must not be used.
* The PCB in [`../pcb/`](../pcb/) uses exactly these two defaults (MQ-8 on
  `ADC-2`/GPIO39, MiCS-5524 on `ADC-1`/GPIO36), so it needs no pin override; a
  breadboard wired the older way (MQ-8 on GPIO36, MiCS-5524 on GPIO39) needs the
  two overrides commented in [`config.yaml`](config.yaml).  The net-to-GPIO table
  is in [`../docs/hardware.md`](../docs/hardware.md).

* The local pre-alarm of `packages/alarm.yaml` uses two pins of its own - a
  passive piezo on `alarm_buzzer_pin` (GPIO33) and the data line of 2 x SK6812 on
  `alarm_led_pin` (GPIO19, 5 V feed) - never the input-only pins 34 - 39. Wiring
  (level shifter, series resistor, bulk capacitor) and the state table:
  [`../docs/local_alarm.md`](../docs/local_alarm.md).

Per-sensor wiring, the load-resistor measurement and the placement rules are in
[`../docs/mq8_sensor_guide.md`](../docs/mq8_sensor_guide.md) and
[`../docs/mics5524_guide.md`](../docs/mics5524_guide.md).

## Calibration

Both components calibrate in **clean air** on the first boot (after
`calibration.delay`, never before `warmup_time` ends) and store the result in
flash (`persist: true`), so a reboot does not repeat the calibration while
hydrogen is present:

| | MQ-8 | MiCS-5524 |
|---|---|---|
| Calibrated value | `R0` (from `ratio_in_clean_air: 70`) | the vendor clean-air reference (`VCC - V_AO`) |
| Delay / samples | 5 min / 50 | 3 min / 10 |
| Pin it with | `r0: <value>` | `air_reference: <value>` |

Force a re-calibration when the wiring, the divider or `rl:` changed - the
`MQ-8 recalibrate` / `MiCS-5524 recalibrate` buttons in Home Assistant, or:

```yaml
esphome:
  on_boot:
    - delay: 5min
    - lambda: id(mq8).request_calibration();   # or id(mics).request_calibration()
```

A request is deferred until `warmup_time` has elapsed (a cold sensor would
capture a wrong reference) and the value stays `unknown` while the calibration is
pending or running - a calibration changes the reference, so the previous value
must not stay visible.

The log prints the result (`R0 = ... kOhm`, `air reference = ...`) and mirrors it
to the `MQ-8 logs` / `MiCS-5524 logs` text sensor; pin it and drop the
`calibration:` block for a deterministic setup. Re-calibrate every few months -
metal-oxide sensors drift. Full workflow:
[`../docs/getting_started.md`](../docs/getting_started.md).

## Validate, flash, test

```bash
cd esphome
esphome config config.yaml            # validate the configuration
esphome compile config.yaml           # full ESP-IDF build
esphome run config.yaml               # flash (OTA or serial)
```

The config fixtures (`esphome config tests/*.yaml`), the host tests of the
measurement math and every lint command are collected in
[`../docs/development.md`](../docs/development.md); the rule book is
[`../.ai/instructions.md`](../.ai/instructions.md) section 3.

## Operations

* **Local pre-alarm** - `packages/alarm.yaml` drives the buzzer and the two
  status LEDs from the MQ-8 value: green below 4 000 ppm, amber + a beep every
  5 s in the 4 000 - 9 999 ppm band, red and *silent* from 10 000 ppm (the MQ-8
  ceiling - a clamped reading may already be past 50 % of the LEL), blue while
  there is no valid reading.  The state is re-applied every 60 s and the
  `alarm test` button plays the melody - see
  [`../docs/local_alarm.md`](../docs/local_alarm.md).
* **Logging** - `logger:` in `config.yaml` runs at `DEBUG` with per-tag
  overrides: the two gas-sensor tags (`mq_gas_sensors`, `mics_5524_gas_sensor`)
  are pinned at `INFO`, so the per-update raw values are off by default - set
  them back to `DEBUG` under `logger.logs` to read them
  (`V=... RS=... ratio=... -> ... ppm`, the applied T/RH correction, the
  calibration result).  The same messages are mirrored to the `MQ-8 logs` /
  `MiCS-5524 logs` text sensors (`log_sensor:` in the packages), so they are also
  visible in Home Assistant without changing the logger level - the per-update
  line at most every 30 s, calibration messages and warnings immediately.
* **Update rate** - the two alarm entities (`H2 (MQ-8)`, `H2 trace (MiCS-5524)`)
  refresh every **1 s**: the MOX heaters run continuously (`wifi.power_save_mode:
  NONE` as well), so slow polling saves nothing.  Every diagnostic entity is
  throttled to 30 s on the device and the DHT22 stays at 60 s (its protocol needs
  ≥ 2 s between reads).  1 Hz data is cheap on the ESP32 (≈ 60 ms of ADC sampling
  per second) but not in the Home Assistant database - see the `recorder:
  exclude:` recipe in
  [`../docs/home_assistant_alerts.md`](../docs/home_assistant_alerts.md).
* **Weekly restart** - `packages/time.yaml` restarts the board every Monday at
  06:00 (SNTP must be synced first). Useful to recover from long-run drift; the
  calibration is in flash and survives it.
* **Watchdog / performance** - `packages/board.yaml` sets a 30 s task watchdog,
  240 MHz, `FREERTOS_HZ 1000` and TLS 1.3. `preferences.flash_write_interval:
  60min` keeps the flash writes (the calibration) gentle; `safe_mode:` and
  `api: reboot_timeout: 30min` provide the usual recovery paths.
* **OTA** - `esphome run config.yaml` over the network; press `EN` once after the
  update so the new firmware starts.

Something wrong? [`../docs/troubleshooting.md`](../docs/troubleshooting.md).

## Documentation

* [`../README.md`](../README.md) - user entry point (what it is, quick start).
* [`../docs/README.md`](../docs/README.md) - index of every document.
* [`components/mq_gas_sensors/README.md`](components/mq_gas_sensors/README.md) - MQ component reference (all options, calibration, behaviour).
* [`components/mics_5524_gas_sensor/README.md`](components/mics_5524_gas_sensor/README.md) - MiCS-5524 component reference.
* [`script/README.md`](script/README.md) - provenance of the vendored ESPHome CI linter.
* [`../.ai/instructions.md`](../.ai/instructions.md) - rule book: lint commands, C++/Python/YAML style, domain and safety rules.

## Credits

The components embed data and formulas from MIT-licensed projects - the full
attribution is in
[`components/mq_gas_sensors/README.md`](components/mq_gas_sensors/README.md),
[`components/mics_5524_gas_sensor/README.md`](components/mics_5524_gas_sensor/README.md),
[`script/README.md`](script/README.md) and
[`../docs/README.md`](../docs/README.md):

* MQUnifiedsensor / MQSensorsLib - the PPM model of the MQ component.
* SolderedElectronics - the `a`/`b` curve table.
* MQDataScience (abcdaaaaaaaaa, MIT) - the T/RH correction and the alternative
  MQ-8 dataset.
* DFRobot_MICS (MIT) - the MiCS-5524 vendor model and its thresholds.
* ESPHome (MIT) - the vendored CI linter in `script/`.

## Licence

This repository's own code, configuration and documentation are dual-licensed
under the **Apache License 2.0** *or* the **MIT License**, at your option - see
[`../LICENSE`](../LICENSE), [`../LICENSE-APACHE`](../LICENSE-APACHE) and
[`../LICENSE-MIT`](../LICENSE-MIT).

Third-party material that is vendored, quoted or derived here keeps its own
licence and is attributed where it is used:

| Material | Where it is used | Licence |
|---|---|---|
| MQUnifiedsensor / MQSensorsLib - PPM model, and the `a`/`b` coefficients cross-checked against its examples | `components/mq_gas_sensors/` (`mq_math.h`, `coefficients.py`) | MIT |
| SolderedElectronics MQ library - `sensorConfigData.h` referenced for the curve table (the values themselves are the datasheet / MQUnifiedsensor fits) | `components/mq_gas_sensors/coefficients.py` | GPL-3.0 (data reference only) |
| MQDataScience (`MQSpaceData` v6.0.0) - temperature/humidity correction model and the alternative MQ-8 dataset | `components/mq_gas_sensors/`, `docs/mqdatascience_comparison.md` | MIT |
| DFRobot_MICS - MiCS-5524 vendor thresholds, gains and measuring ranges | `components/mics_5524_gas_sensor/coefficients.py` | MIT |
| ESPHome - `script/ci-custom.py` and `script/helpers.py`, vendored verbatim | `script/`, used by the `ci-custom` pre-commit hook | MIT |
| ESPHome framework itself (not vendored; the firmware is compiled against it like any ESPHome project) | the build | MIT for the Python codebase, GPLv3 for the C++/runtime files |
| Datasheet fits quoted from community sources (e.g. the MiCS-5524 CO two-point fit) | `docs/mics5524_conversion.md`, `docs/mq8_h2_curve.md` | facts / derivations - attributed to the sources in `docs/README.md` |

The ESP-IDF / ESP32 toolchain, the Espressif SDK and the MQ/MiCS sensor
datasheets are used under their owners' terms and are not redistributed here.

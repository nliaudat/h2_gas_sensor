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

## Runtime

| Symptom | Likely cause | What to do |
|---|---|---|
| state stays `unknown`, log says `R0 unknown` / `no air reference` | no `calibration:` block and no pinned reference, or the calibration has not run yet (delay / warm-up window) | wait for the delay or the burn-in, or pin `r0:` / `air_reference:` |
| warning `analog output reads 0.0000 V` | `AO` not connected, module not powered from 5 V, wrong divider node, MiCS `EN` polarity | re-check the wiring and `inverted:` |
| ppm far too low, or nearly flat | `rl:` does not match the module (measure the resistor between `AO` and GND), or the divider ratio changed without updating `r1`/`r2` | set `rl:` / `divider:` to the measured values |
| ppm pinned at `max_ppm` (10 000 for the MQ-8, 1 000 for the MiCS) | the gas is outside the range, the curve does not match the gas, or the divider is wrong | check `gas:` / `curve:`, then the divider |
| readings jump around | ESP32 ADC noise | raise `samples:` (e.g. 16), add a 100 nF capacitor from the ADC pin to GND, or use an ADS1115 |
| values drift over weeks | normal metal-oxide drift | re-calibrate in clean air - see [`getting_started.md`](getting_started.md) |
| `MQ-8 T/RH correction` stuck at 1.0000 | the T/RH sensor has no state yet, the `temperature:`/`humidity:` links are commented or the compensation block is not uncommented in `packages/mq8.yaml`, a wrong I2C address, or an unreadable DHT11 data line | check the log warning, the links and pins in `packages/mq8.yaml` (`mq8_i2c_sda`/`mq8_i2c_scl`, `mq8_sht4x_address`, `mq8_dht_pin`), or the pull-up of the DHT11 |
| the MiCS-5524 reacts to something that is not hydrogen | it has a single output for CO, H2, ethanol, ammonia and methane | treat it as a trend sensor; the MQ-8 is the reference for alarms |
| the readings are plausible but the room smells of hydrogen | the sensor sits at the wrong height | hydrogen accumulates at the top: mount it at the highest point of the room or enclosure |

## Reading the log

`esphome logs config.yaml` (or the web log) prints one line per update at
`DEBUG` level, prefixed with `[D][tag]`. The two gas-sensor tags are pinned at
`INFO` in [`../esphome/config.yaml`](../esphome/config.yaml) to keep the log
readable; set `mq_gas_sensors` / `mics_5524_gas_sensor` back to `DEBUG` under
`logger.logs` to read the chain (illustrative values):

```
[D][mq_gas_sensors]: 'MQ-8 H2': V=1.234 V, RS=12.345 kOhm, ratio=42.100 (correction=0.9987) -> 74.5 ppm
[D][mics_5524_gas_sensor]: 'H2': V_AO=1.234 V, x=0.5670, RS=8.800 kOhm, ratio=0.9980 (dfrobot) -> 0.0 ppm
```

* `V` is the scaled voltage at the pin (after `voltage_multiplier`) - use it to
  verify the divider ratio;
* `ratio` is RS/R0 for the MQ-8, or the vendor ratio for the MiCS (≈ 1.0 in
  clean air, dropping when a reducing gas arrives);
* `correction` is the T/RH factor (1.0000 = none);
* the ppm value at the end is what the sensor entity publishes.

# Hardware - the PCB in `pcb/`

The `pcb/` folder holds the finished carrier board for this project. It takes an
**ESP32 devkit** in two female sockets and carries everything the packages need:
the analog front ends of the **MQ-8** and the **MiCS-5524**, the optional
**DHT22**, the local pre-alarm (piezo + 2 × SK6812) and the power input. The
firmware in [`../esphome/`](../esphome/) runs on it unchanged: the `mq8_pin` /
`mics_pin` defaults of the packages (GPIO39 / GPIO36) are exactly the two analog
pins of this board (see [Pin mapping](#pin-mapping)).

| | |
|---|---|
| Board | "Board1" V1.0, EasyEDA export of 2026-09-25, 2 layers |
| Size | 84.1 × 59.3 mm (printed on the layout drawing) |
| MCU | ESP32 devkit (38-pin) in 2 × 20-pin female sockets - `az-delivery-devkit-v4`, `nodemcu-32s`, ... |
| Sensor front ends | MQ-8 (`VCC`/`GND`/`D0`/`A0` header, `A0` through 10k/20k), MiCS-5524 (`5V`/`GND`/`A0`/`En` header, `A0` through 10k/20k) |
| On board | DHT22 header, 4 kHz passive piezo + SS8050 driver, 2 × SK6812MINI-HS, I2C/UART/shift-register spares |
| Power | 5 - 18 V DC screw terminal -> XL1509 step-down *module* -> 5 V rail; the devkit's own regulator makes the 3.3 V |
| Fab files | JLCPCB Gerber set + 2 drill files ([`Gerber_PCB1_2026-09-25.zip`](../pcb/Gerber_PCB1_2026-09-25.zip)) |

> The PDFs in `pcb/` are the *published* copy of the EasyEDA project - the project
> itself is not in the repository. Everything in this document was read from those
> files; where this text and the schematic disagree, the schematic wins.

## Files in `pcb/`

| File | Content | Use it for |
|---|---|---|
| `SCH_Schematic1_2026-09-25.pdf` | 10 sheets, A4 landscape (EasyEDA PDF export) | the netlist truth: which net reaches which socket pin |
| `PCB_PCB1_2026-09-25.pdf` | 9 pages: layout views (1, 7), EasyEDA/JLCPCB BOM and LCSC part/datasheet pages (3 - 6, 8) | ordering the parts, checking the placement |
| `PCB_PCB1_2026-09-25.png` | board render (silkscreen + copper), dimensions printed on the drawing | a quick look, the silkscreen texts |
| `Gerber_PCB1_2026-09-25.zip` | `Gerber_TopLayer.GTL`, `Gerber_BottomLayer.GBL`, top/bottom silkscreen + solder/paste masks, `Gerber_BoardOutlineLayer.GKO`, document + drill drawing, `Drill_PTH_Through.DRL`, `Drill_PTH_Through_Via.DRL`, `FlyingProbeTesting.json`, `How-to-order-PCB.txt` | the fab (JLCPCB), flying-probe test |

## The sheets

The schematic export mixes the 2026 revision of this board with sheets carried
over from the earlier revisions and from the author's other projects (the title
blocks still name "Barbarossa" 2021 and adrirobot 2023).

| Page | Title | Content | On this PCB? |
|---|---|---|---|
| 1 | devkit socket sheet (`H2_sensors` REV 1.2, 2021-01-25) | 2 × 20-pin sockets, the `ADC-1` … `ADC-4` flags, `DHT22`, `buzzer`, `LED`, `SDA`/`SLC`, `TX`/`RX`, `SRCLK`/`RCLK`/`SER`, 3V3/5V/GND | yes - the MCU sheet |
| 2 | power (same 2021 file) | USB-C + 5.1 k CC resistors, XL1509 module, AMS1117-3.3, 470 µF, "others inputs" | partly: the 5-18 V input and the XL1509 module pads are used, the USB-C/AMS1117 branch is not in the BOM |
| 3 | `esp32-c3 supermini` (updated 2026-09-21) | the same nets on an ESP32-C3 super-mini (female + male headers) | no - the alternative MCU variant of the same design |
| 4 | `MQ-8 sensor` | `MQ8header` (4 pins), R3 10k + R4 20k + C1 100nF, net `ADC-2` | yes |
| 5 | `mics-5524 sensor` | `MICSheader` (4 pins), R5 10k + R6 20k + C2 100nF, net `ADC-1` | yes |
| 6 | `holes` | the mounting holes | yes |
| 7 | `led` | U6, U7 `SK6812MINI-HS` plus alternative `SK6812SIDE-A` footprints, net `LED` | U6/U7 only |
| 8 | `buzzer` (adrirobot REV 1.0, 2023-05-05) | Q2 SS8050 + R7 1k, net `buzzer` | yes |
| 9 | `LCD` | 8-pin `HS96S01A` display connector | no - no such header in the BOM |
| 10 | `DHT22` | 3-pin header (`3V3`/`GND`/data), net `DHT22` | yes |

The two analog inputs are the point where the sheets meet: sheet 4 (MQ-8) and
sheet 5 (MiCS-5524) each build a divider and name its output `ADC-2` /
`ADC-1`; sheet 1 turns those two nets into two socket pins, and the devkit turns
those two socket pins into two GPIOs.

## Pin mapping

The devkit sits in the sockets, so every net is defined *at the socket*: sheet 1
prints the socket pin name next to the net flag, and the devkit's own header
pinout decides the GPIO. The table is the whole interface - the pin names are the
ones silkscreened on the board.

| Net | Comes from | Socket pin (silkscreen) | GPIO | ADC1 | Firmware substitution |
|---|---|---|---|---|---|
| `ADC-1` | **MiCS-5524** `A0` (R5 10 k in series, R6 20 k to GND, C2 100 nF) | left socket, 3rd pin (`SENSORVP`) | **GPIO36** | CH0 | `mics_pin` |
| `ADC-2` | **MQ-8** `A0` (R3 10 k in series, R4 20 k to GND, C1 100 nF) | left socket, 4th pin (`SENSORVN`) | **GPIO39** | CH3 | `mq8_pin` |
| `ADC-3` | spare | left socket, 5th pin (`IO34`) | GPIO34 | CH6 | - |
| `ADC-4` | spare | left socket, 6th pin (`IO35`) | GPIO35 | CH7 | - |
| `DHT22` | DHT22 data (`3V3`/`GND`/data header) | left socket, 7th pin (`IO32`) | GPIO32 | - | `dht22_pin` |
| `buzzer` | base of Q2 through R7 (1 k) | left socket, 8th pin (`IO33`) | GPIO33 | - | `alarm_buzzer_pin` |
| `LED` | DIN of U6 (U7 chained behind it) | right socket, 8th pin (`IO19`) | GPIO19 | - | `alarm_led_pin` |
| `SDA` / `SLC` | spare I2C (ambient T/RH sensor) | right socket, 6th / 3rd pin (`IO21` / `IO22`) | GPIO21 / GPIO22 | - | `mq8_i2c_sda` / `mq8_i2c_scl` |
| `TX` / `RX` | devkit UART | right socket, 4th / 5th pin (`TXD0` / `RXD0`) | GPIO1 / GPIO3 | - | - |
| `SRCLK` / `RCLK` / `SER` | spare shift-register signals (74HC595 style) | right socket, 10th … 12th pin (`IO5` / `IO17` / `IO16`) | GPIO5 / GPIO17 / GPIO16 | - | - |
| `5V`, `3V3`, `GND` | devkit power pins | both sockets | - | - | - |

What follows from the table:

* All four ADC nets land on **input-only** pins (GPIO34 - 39) - the "Input only
  pins" notes of sheet 1 mark exactly those rows.
* `ADC-3`, `ADC-4` and the right socket's `IO18` pin are free for a second or
  third sensor (a second `mq_gas_sensors` entry on another MQ module).
* The I2C spare sits on GPIO21/GPIO22, i.e. the pins the `mq8_i2c_sda` /
  `mq8_i2c_scl` keys of [`../esphome/packages/mq8.yaml`](../esphome/packages/mq8.yaml)
  use (they and the SHT4x example ship commented): plug an SHT4x breakout on the
  `SDA`/`SLC` pins and uncomment both.
* Never move `DHT22` onto an input-only pin: the 1-wire protocol drives the line
  (same rule as in [`getting_started.md`](getting_started.md)).

### The two analog inputs

The schematic pairs the **MQ-8 with `ADC-2`** and the **MiCS-5524 with `ADC-1`**,
which puts the MQ-8 on **GPIO39** and the MiCS-5524 on **GPIO36** - the "crossed"
way round compared with the numbering of the two nets. The packages ship with
exactly those two pins (`mq8_pin: GPIO39`, `mics_pin: GPIO36`), so **a board built
from this PCB needs no pin override**.

What it does need is a calibration: `R0` and the MiCS air reference are stored in
flash *per measurement chain* and belong to the pin that was measured - press the
`MQ-8 recalibrate` / `MiCS-5524 recalibrate` buttons (see
[`getting_started.md`](getting_started.md)) after any wiring change.

A **hand-wired** setup is a different story: the earlier revision of this board (and
the guides written for it) put the MQ-8 on `ADC-1`/GPIO36 and the MiCS-5524 on
`ADC-2`/GPIO39. If your breadboard follows those, uncomment the matching overrides
in [`../esphome/config.yaml`](../esphome/config.yaml):

```yaml
substitutions:
  mq8_pin: GPIO36   # MQ-8 A0  -> ADC-1 -> left socket pin 3 (SENSOR_VP, ADC1_CH0)
  mics_pin: GPIO39  # MiCS A0  -> ADC-2 -> left socket pin 4 (SENSOR_VN, ADC1_CH3)
```

Two ways to find out which pin a sensor is really on:

* with a multimeter: the `A0` pad of the `MQ8header` is continuous with the left
  socket's 4th pin (`SENSORVN`), the `A0` pad of the `MICSheader` with the left
  socket's 3rd pin (`SENSORVP`). The silkscreen prints `ADC-2` next to the MQ-8
  divider and `ADC-1` next to the MiCS divider, and `SENSORVP` / `SENSORVN` /
  `IO34` / `IO35` / `IO32` / `IO33` next to the sockets;
* without one: breathe on one sensor and watch which Home Assistant entity moves -
  a swapped pair publishes both values, it just attaches the wrong curve to each.

## Analog front ends

Sheet 4 (MQ-8) and sheet 5 (MiCS-5524) are built the same way:

```
sensor A0 ──[ R3 / R5 = 10 kΩ ]──┬── ADC-n ──> left socket (SENSORVP / SENSORVN)
                                 │
                       [ R4 / R6 = 20 kΩ ]     C1 / C2 = 100 nF in parallel to it
                                 │
                                GND
```

* The divider ratio is 20/(10+20) = 2/3, so the pin sees 2/3 of the module
  output: 5 V → 3.33 V. That is exactly the `{r1: 10.0, r2: 20.0}` /
  `voltage_multiplier: 1.5` default of both packages and the "10 kΩ in series,
  20 kΩ to ground" recommendation of the guides - a stock board needs no YAML
  change for the divider.
* C1/C2 filter the divider node (source impedance 10 k ∥ 20 k = 6.7 kΩ → ~0.7 ms),
  which is why the default sampling of the packages (`samples:`, `sample_interval:`)
  can stay as it is.
* 3.33 V is a hair above the 3.3 V "recommended maximum" of the ESP32 ADC, so the
  very top of the range can clip - both packages declare `adc_input_max: 3.33` for
  that reason. Fit 10 k / 10 k (2.5 V) instead of the 20 k if you want the whole
  range unambiguous and set `mq8_divider_r2` / `mics_divider_r2` to `10.0`.
* The MQ-8 header is `VCC` (5 V) / `GND` / `D0` / `A0`: only `A0` is used, the
  digital output stays open. It sits inside the module outline printed as `MQ-9` -
  the MQ-8 and the MQ-9 module share the 4-pin interface and the outline, so
  either one fits the header.
* The MiCS header is `5V` / `GND` / `A0` / `En`. `En` is only broken out: the
  schematic does not route it to a GPIO, so leave `mics_enable_pin` commented
  until your module really needs an enable level (most breakouts are enabled by
  pulling it LOW, hence `inverted: true` in the example).

## Power

```
5-18 V DC ─[ MX126, 5 mm screw terminal ]─> XL1509 step-down MODULE (IC2) ─> 5 V rail
   (the USB-C + 5.1 k CC / AMS1117-3.3 branch of sheet 2 is drawn but not      │
    populated - no such part in the BOM)                                       │
        ├── MQ-8 heater (~150 mA)        ├── SK6812 U6/U7 (~120 mA at full white)
        ├── MiCS-5524                    ├── 470 µF + 220 µF + 10 µF + 100 nF
        └── devkit "5V" pin -> its own 3.3 V regulator -> the ESP32
```

* The 5 V rail is shared by everything: add the heater, the LEDs and the devkit up
  before you pick a supply, and keep the LED brightness low (the alarm effects
  already use 10 - 40 %).
* 3.3 V is not generated on the board - the devkit's regulator makes it, and the
  only 3.3 V consumer is the DHT22 header.
* The devkit can also run from its own USB connector; feeding both at the same
  time depends on your devkit (diode or ideal-diode on some boards) - pick one
  source if you are unsure.

## Local pre-alarm hardware

```
                                   5 V ────● 4 kHz passive piezo (17 mm disc, HNR-1707)
GPIO33 ──[ R7 = 1 kΩ ]──┬── B   Q2 SS8050
                        └───── E ──── GND
```

* The board buffers the piezo with a **transistor** (Q2 + R7), while
  [`local_alarm.md`](local_alarm.md) describes driving the disc straight from the
  pin. For RTTTL (`alarm_buzzer_pin: GPIO33`) that makes no difference except that
  the drive is inverted and the disc now sees ~5 V instead of 3.3 V - i.e. louder.
  Tune `gain:` in [`../esphome/packages/alarm.yaml`](../esphome/packages/alarm.yaml)
  if the tone is harsh, and keep a **passive** piezo (the 4 kHz HNR-1707 of the
  BOM): an active buzzer only clicks, a magnetic one would also need a flyback
  diode.
* The two status LEDs are `U6` and `U7` (`SK6812MINI-HS`, chained DIN → DOUT →
  DIN) on the `LED` net (GPIO19), fed from 5 V via the `LED`/`5V` pads:
  `alarm_led_count: 2`, chipset `SK6812`, `GRB`. The alternative
  `SK6812SIDE-A` footprints of sheet 7 are not fitted.
* The board does **not** carry the 100 - 500 Ω series resistor, the 10 kΩ data
  pull-down or a level shifter recommended in [`local_alarm.md`](local_alarm.md):
  3.3 V data into a 5 V SK6812 stays marginal. Keep the data trace short and add
  the pull-down / series part externally if the first LED flickers or latches a
  colour at boot.

## What else is on the board

* `H-IN+1` / `H-IN-1` and `H-OUT+1` / `H-OUT-1`: four 2-pin headers for free use -
  they belong to no net in the firmware.
* `SRCLK` / `RCLK` / `SER` (right socket) and the LCD sheet 9 are leftovers of the
  author's earlier display project; the shift-register chain and the `HS96S01A`
  header are not populated and not supported by the firmware.
* Sheet 3 shows the same design on an **ESP32-C3 super-mini** - an alternative MCU
  variant, not this board. Do not copy its pin numbers: on the C3 the `ADC-n` nets
  land on different pins.
* There is no protection hardware on the board: no reverse-polarity protection, no
  fuse, no TVS diode, no Ex/ATEX rating. Feed it from a fused supply, keep it in a
  housing, and read the safety notice in [`../README.md`](../README.md).

## Bill of materials

From the BOM pages of `PCB_PCB1_2026-09-25.pdf` (the LCSC part numbers and the
JLCPCB classes are in there):

| # | Qty | Part | Designator |
|---|---|---|---|
| 1 | 1 | Screw terminal 5 mm, 2P (MX126-5.0-2P) | 5-18V-input |
| 2 | 1 | Passive piezo 4 kHz, 17 mm (HNR-1707) | BUZZER |
| 3 | 2 | 100 nF 0805 | C1, C2 |
| 4 | 2 | 470 µF / 10 V electrolytic | C3, C36 |
| 5 | 1 | 100 nF 0603 | C6 |
| 6 | 1 | 10 µF 0603 | C23 |
| 7 | 1 | 220 µF / 16 V electrolytic | C24 |
| 8 | 1 | 3-pin female header, 2.54 mm | DHT22 |
| 9 | 2 | 20-pin female header, 2.54 mm | ESP-20P-LEFT, ESP-20P-RIGHT |
| 10 | 4 | 2-pin header, 2.54 mm | H-IN+1, H-IN-1, H-OUT+1, H-OUT-1 |
| 11 | 2 | 4-pin female header, 2.54 mm | MICSheader, MQ8header |
| 12 | 1 | SS8050 (SOT-23) | Q2 |
| 13 | 2 | 10 kΩ 0402 | R3, R5 |
| 14 | 2 | 20 kΩ 0805 | R4, R6 |
| 15 | 1 | 1 kΩ 0603 | R7 |
| 16 | 2 | `SK6812MINI-HS` | U6, U7 |

The ESP32 devkit, the MQ-8/MiCS-5524 modules, the DHT22 and the piezo disc are
plugged in, not soldered on - see the parts list of [`../README.md`](../README.md).

## See also

* [`../README.md`](../README.md) - what the project is, the wiring at a glance.
* [`getting_started.md`](getting_started.md) - secrets, substitutions, packages, first flash, calibration.
* [`mq8_sensor_guide.md`](mq8_sensor_guide.md) / [`mics5524_guide.md`](mics5524_guide.md) - the sensors themselves: burn-in, load resistor, placement.
* [`local_alarm.md`](local_alarm.md) - buzzer/LED states, thresholds and the wiring caveats.
* [`../esphome/readme.md`](../esphome/readme.md) - the firmware side: packages, calibration, operations.

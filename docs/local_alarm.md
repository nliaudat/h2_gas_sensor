# Local pre-alarm - buzzer and status LEDs

A *local* early warning next to the sensor: a passive piezo on an RTTTL buzzer
output and two SK6812 LEDs that mirror the MQ-8 reading. It keeps working when
Wi-Fi, Home Assistant or the broker is down - which is the whole point of a
local annunciator - while the alert automations of
[`home_assistant_alerts.md`](home_assistant_alerts.md) stay exactly as they are.
The thresholds it uses are the ones of [`h2_thresholds.md`](h2_thresholds.md).

> **Not a gas detector, not an Ex/ATEX device.** This is a hobby-grade indicator
> on an uncertified ESP32 board. It is a *pre*-alarm: it cannot measure the
> explosive range (the real LEL is 40 000 ppm, the MQ-8 stops at 10 000 ppm) and
> it must never replace a certified alarm or the Home Assistant automations.

Package: [`../esphome/packages/alarm.yaml`](../esphome/packages/alarm.yaml),
included by `config.yaml` (optional hardware - remove the include if there is no
buzzer/LED strip). It **requires** `packages/mq8.yaml` and must stay *after* it
in `config.yaml`, because it merges its band logic into `id: mq8` with
`!extend` (the same pattern as `packages/dht22.yaml`). The minimal real
installation is `mq8` + `alarm`; `tests/test_alarm_package.yaml` is the fixture
that keeps that combination validated.

## Behaviour

| MQ-8 value | Level | LEDs (2 x SK6812) | Buzzer |
|---|---|---|---|
| `unknown` (warm-up, calibration pending/running, invalid reading) | no reading | blue, steady | silent |
| below 4 000 ppm | ok | green, 2 s heartbeat | silent |
| 4 000 - 9 999 ppm (10 - 25 % of the LEL) | early warning | amber, 400 ms on / 1 600 ms off | one short beep every 5 s |
| 10 000 ppm and above (25 % of the LEL, the MQ-8 ceiling) | danger | red, 250 ms on / 250 ms off | **silent** |

The value is the published `H2 (MQ-8)` ppm - with the optional T/RH
compensation that is the corrected one, which is the best estimate (see
[`temperature_humidity_correction.md`](temperature_humidity_correction.md)).

The invalid case is checked *first*: a dead sensor shows blue, never the green of
clean air (the same rule the component follows by publishing `unknown` instead of
`0 ppm`). Recovering from a fault re-evaluates the band, so the LEDs come back on
their own.

## Why the buzzer stops at 10 000 ppm

`h2_thresholds.md` calls 20 000 ppm (50 % of the LEL) the point of "immediate
intervention", and one would naively silence the buzzer there. That does not work
with this sensor: `packages/mq8.yaml` clamps at `max_ppm: 10000`, so a published
10 000 ppm means **"10 000 ppm or more, upper bound unknown"** - the true
concentration may already be past 50 % of the LEL (the clamp is asserted by
`clamp_ppm(50000, 0, 10000) -> 10000` in `mq_math_test.cpp`). A buzzer that kept
sounding there would be claiming a measurement the sensor can no longer make, and
a rule "silence at 20 000" could never fire at all.

So the audible warning covers the band the sensor can actually resolve
(4 000 - 9 999 ppm, 10 - 25 % of the LEL) and the annunciator goes visual-only
above it, where the response belongs to the certified detector and to the Home
Assistant automations. Moving `alarm_danger_ppm` to `20000` only makes sense
together with an MQ-8 `max_ppm` above 20 000, which extrapolates the curve
outside its documented 100 - 10 000 ppm range.

This is a policy, not a certified safety function: silencing a hobby buzzer does
not make an uncertified installation safe, and the ESP32 board itself is not
Ex-rated.

## Entities

| Entity (suffix of `friendly_name`) | Kind | Notes |
|---|---|---|
| `alarm LEDs` | light (diagnostic) | one entity for the 2-LED strip; its *effect* is the current level (`ok`, `pre-alarm`, `danger`, `no-reading`), so Home Assistant shows the state too |
| `alarm test` | button (config) | plays the pre-alarm melody so the buzzer can be checked without hydrogen |

The device re-applies the LED state every `alarm_led_refresh_interval` (60 s), so
a stray command (or the light toggled from Home Assistant) cannot leave the LEDs
lying for long. There is deliberately **no mute switch**: nothing in Home
Assistant can silence the local pre-alarm without editing `packages/alarm.yaml`.

The level changes and the invalid state are logged to the console
(`H2 pre-alarm band 4000 - 9999 ppm ...`), so `esphome logs config.yaml` shows
when the annunciator changed state.

## Wiring

Buzzer (passive piezo, `alarm_buzzer_pin` = GPIO33):

```
GPIO33 -------------+--------|>|----- GND        passive piezo, no series part needed
                    |   piezo
                    +--- optional 100 Ohm in series (softens the loudest notes)
```

* Use a **passive** piezo (a bare disc without an internal oscillator). An
  "active" buzzer ignores the note frequency and only clicks.
* A piezo draws a few mA, so it can sit on the pin directly. A magnetic/coil
  buzzer draws far more (tens of mA, with an inductive kick): drive it through a
  transistor (and a flyback diode), never straight from the GPIO.
* `gain:` in the package sets the loudness (leave headroom: a clipped signal
  sounds harsh and heats the piezo).
* The PCB in [`../pcb/`](../pcb/) drives the piezo through a transistor (Q2 SS8050
  + 1 kΩ base resistor) instead of directly from the pin, and it does not fit the
  data-line series resistor or the 10 kΩ pull-down of the LED wiring above - the
  melody and the pin are unchanged, but the disc is louder (it sees ~5 V) so
  `gain:` may need a nudge.  See [`hardware.md`](hardware.md).

LEDs (2 x SK6812, `alarm_led_pin` = GPIO19):

```
5V --- [ bulk 470 - 1000 uF ] --- SK6812 #1 DIN <- 100 - 500 Ohm <- GPIO19
                                    DOUT -> SK6812 #2 DIN
GND ---- common with the ESP32 ----+  100 nF across each LED's VDD/GND
GPIO19 --- [ 10 kOhm to GND ]  (pull-down: no random colour while booting)
```

* The strip wants 5 V. The ESP32 drives 3.3 V data while a 5 V SK6812 expects
  about 0.7 x VDD = 3.5 V, which is marginal: keep the data wire short and add
  either a 74AHCT125 level shifter or a silicon diode in the strip's 5 V feed
  (4.3 V still lights them and 3.0 V is then a valid high level). Symptoms of a
  marginal data line are flickering or a stuck first LED.
* Do not power the strip from 3.3 V (below the SK6812 minimum) and never route
  the data line to the input-only pins 34 - 39.
* Keep the brightness low (the effects in the package use 10 - 40 %): two LEDs at
  full white are ~120 mA and the 5 V rail is shared with the MQ-8 heater
  (~150 mA) on a 500 mA USB supply.
* The pins must not collide with `mq8_pin` (GPIO39), `mics_pin` (GPIO36),
  `dht22_pin` (GPIO32) or the optional example pins of the other packages.

## Tuning

Everything lives in `substitutions:` at the top of `packages/alarm.yaml`:

| Key | Default | Meaning |
|---|---|---|
| `alarm_buzzer_pin` | `GPIO33` | LEDC/PWM output of the piezo |
| `alarm_led_pin` | `GPIO19` | data line of the SK6812 strip |
| `alarm_led_count` | `2` | number of LEDs in the strip |
| `alarm_led_chipset` | `SK6812` | `WS2811` / `WS2812` / `SK6812` / `APA106` / `SM16703` |
| `alarm_led_channel_colors` | `GRB` | `GRB` for RGB, `GRBW` for an SK6812 RGBW |
| `alarm_early_ppm` | `4000` | first annunciated band (10 % of the LEL): amber LEDs, beeps |
| `alarm_danger_ppm` | `10000` | from here on the buzzer is silent (see above) |
| `alarm_beep_interval` | `5s` | how often the pre-alarm beep repeats |
| `alarm_led_refresh_interval` | `60s` | how often the LED state is re-asserted (self-heal of a stray command) |
| `alarm_rtttl` | `two_short:d=4,o=5,b=100:16e6,16e6` | the melody (any RTTTL string) |

The colours and blink patterns of the four states are the `effects:` list of the
`light:` entry; each state is a `strobe` effect, so changing the timing there does
not touch the logic. Moving the two thresholds moves the *policy* - do it
deliberately and update [`h2_thresholds.md`](h2_thresholds.md) with it.

## Checking it

* Press `alarm test` in Home Assistant: the melody plays (the wiring and the
  volume can be verified without hydrogen). Select an effect on the `alarm LEDs`
  light entity to see a colour; the `alarm_led_refresh_interval` self-heal (60 s)
  restores the real state.
* `esphome logs config.yaml` (or the web log) prints a line on every band change,
  e.g. `H2 pre-alarm band 4000 - 9999 ppm (10 to 25 percent of the LEL)`.
* The states can be provoked without gas: temporarily set `alarm_early_ppm` below
  the clean-air reading (about 52 ppm) to see the amber/beep state, and
  `alarm_danger_ppm` below it to see the red/silent state - then put both back.
  Never leave lowered thresholds in a flashed firmware.
* The LEDs show blue while the MQ-8 has no valid value: that is normal during the
  warm-up and the first calibration (up to `calibration.delay` + `samples`), and a
  warning sign afterwards - check the `MQ-8 logs` text sensor and
  [`troubleshooting.md`](troubleshooting.md).

## Validation

```bash
cd esphome
esphome config tests/test_alarm_package.yaml   # mq8 + alarm, minimal combination
esphome config config.yaml                     # mq8 + dht22 + alarm (double !extend)
esphome compile config.yaml
```

The package only adds YAML: no component code changes, so the host tests
(`mq_math_test.cpp`, `mics_math_test.cpp`) are unaffected. The threshold numbers
live in the package's `substitutions:` and are the only place the policy is
defined; the same numbers are quoted in this document and in
[`h2_thresholds.md`](h2_thresholds.md).

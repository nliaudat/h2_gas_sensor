# Local patches for the upstream pull requests

The project builds `mq_gas_sensors` and `tsdb` straight from
`esphome/config.yaml` (`github://pr#20896` / `github://pr#20892`), so a change in
a PR head reaches this board unchanged. One of them needs a small local patch to
build the board in `pcb/`:

## `pr20896-adc-input-max.patch` - `adc_input_max:` must stay authoritative

* **Applies to:** `esphome/components/mq_gas_sensors/sensor.py` and its
  `tests/component_tests/mq_gas_sensors/test_mq_gas_sensors.py` at PR #20896
  head `320bab784c7fbeb1cc0509855f10473394a7fba5`.
* **Why:** `_effective_input_max()` clamps a declared `adc_input_max:` to the
  range the ADC measures at its attenuation (2.45 V at 12 dB on the ESP32 of
  this board). The PCB feeds both gas sensors through a 10k/20k divider, so a
  5 V sensor output reaches 3.33 V on the pin - the firmware *declares* that
  trade-off (`adc_input_max: 3.33`, the top of the range clips) and the
  validator then refuses to build. The merged `mics_5524` component treats the
  declared value as authoritative, and this patch aligns `mq_gas_sensors` with
  it: the value is used as declared and a value above the attenuation range is
  logged at `DEBUG` instead of rejected.
* **Consequences:** `esphome config` builds again on this hardware; the top of
  the MQ-8 range (AO above ~3.7 V, i.e. a saturated sensor) reads non-linearly -
  the same trade-off the MiCS-5524 package has always documented.
* **Apply** in a checkout of `esphome/esphome` at that revision:

  ```bash
  git apply --stat  patches/pr20896-adc-input-max.patch   # what it touches
  git apply --check patches/pr20896-adc-input-max.patch   # applies cleanly?
  git apply         patches/pr20896-adc-input-max.patch
  ```

  Then run the component tests (`pytest tests/component_tests/mq_gas_sensors`).
  The patch also rewrites `test_declared_range_cannot_widen_the_sampler` into
  `test_declared_range_is_authoritative`, so the suite asserts the new rule
  instead of the old one.

Until the patch (or an equivalent upstream change) lands, `esphome config`
stops at `packages/mq8.yaml` with "the sensor output can reach 5.00 V and the
configured wiring maps that to 3.33 V at the ADC input" - see
[`../docs/development.md`](../docs/development.md) and the symptom table in
[`../docs/troubleshooting.md`](../docs/troubleshooting.md).

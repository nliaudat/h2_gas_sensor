# Local patches for the upstream pull requests

The project builds `mq_gas_sensors` and `tsdb` straight from
`esphome/config.yaml` (`github://pr#20896` / `github://pr#20892`), so a change in
a PR head reaches this board unchanged. Nothing is patched at the moment.

## `pr20896-adc-input-max.patch` - retired

The patch made a declared `adc_input_max:` authoritative, the rule the merged
`mics_5524` component follows. PR #20896 does that itself since head
`f493561faf8cef8700af37b821ba8f1675726a04`, so the patch - and the step that
re-applied it after every `refresh: always` - is gone. A declared range above the
2.45 V the ADC measures at `attenuation: 12db` is accepted, reported as a warning
and used as the range the divider is checked against: see
[`../docs/development.md`](../docs/development.md) and the symptom table in
[`../docs/troubleshooting.md`](../docs/troubleshooting.md).

Nothing in this directory is required by the build.

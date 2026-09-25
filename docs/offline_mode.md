# Offline mode - the board without Home Assistant

The board is built for a battery room that may have no usable Wi-Fi, so the same
firmware covers two situations:

| Mode | Who is on the network | How you reach the board |
|---|---|---|
| **hassio / normal** | one of the `wifi.networks` of [`../esphome/packages/wifi.yaml`](../esphome/packages/wifi.yaml) | Home Assistant discovers it over the native API; the dashboard is at `http://<name>.local/` (or the IP the router handed out) |
| **offline** | the board's own **open** access point `<name> Fallback`, address `192.168.4.1` | connect a phone or laptop to that AP and open `http://192.168.4.1/` - no internet, no router, no Home Assistant |

Both come from the same build: the board always tries the known networks first
and only falls back to its own access point when none of them is reachable. The
radio side is [`../esphome/packages/wifi.yaml`](../esphome/packages/wifi.yaml),
the HTTP side is
[`../esphome/packages/web.yaml`](../esphome/packages/web.yaml).

## The open fallback AP

```yaml
ap:
  ssid: "${name} Fallback"
  # no `password:` on purpose
```

* **No password** - with the `password:` key absent ESPHome starts the SoftAP
  with `WIFI_AUTH_OPEN`. This is deliberate: a responder must be able to join the
  board in a room with no infrastructure and read the value on the spot.
* **It is a fallback, not a second interface** - the AP comes up only after
  `ap_timeout` (ESPHome's default is **90 s**) with no station connected, i.e.
  after all three `networks` were tried. Uncomment `ap_timeout:` under `ap:` to
  change that wait (`0s` disables the fallback AP completely).
* **It starts at most once per boot.** Once one of the known networks has
  connected, the AP is switched off and is not brought back until the next boot -
  see "AP and station at the same time" below. A power-cycle, or the Monday
  06:00 restart of
  [`../esphome/packages/time.yaml`](../esphome/packages/time.yaml), re-arms it.
* The AP address is the ESPHome default `192.168.4.1`. The captive portal's DNS
  answers every queried name with that address, so a browser that opens any URL
  lands on the board - `http://192.168.4.1/` is the address to remember.

Connecting: pick `<name> Fallback` in the Wi-Fi list (it is open), ignore the
"no internet" warning - the dashboard is on the device itself.

## The dashboard (`packages/web.yaml`)

```yaml
web_server:
  port: 80
  version: 3
  local: true    # embed the UI in the firmware
  log: false
  ota: false
captive_portal:
```

* `local: true` is the important one for offline use: it embeds the complete web
  UI (HTML + JavaScript, gzipped) **in the firmware** and serves it from `/`.
  Without it ESPHome points the page at `https://oi.esphome.io/v3/www.js`, and a
  browser on the offline AP - which has no internet - shows a blank page. With
  it the dashboard renders with no network beyond the AP.
* `version: 3` is the modern UI (version 1 is deprecated and removed in ESPHome
  2027.1).
* `log: false` - streaming the log to the browser costs RAM/CPU while the gas
  entities update at 1 Hz; read the log with `esphome logs` or in Home Assistant
  instead.
* `captive_portal:` answers the "is there a captive portal?" probe that phones
  and laptops send on a fresh AP and serves the Wi-Fi credentials form, so a
  board can be moved to another network without a serial cable. It shares the
  port-80 server of `web_server` and keeps the web OTA platform enabled.
* Removing the `web: !include ...` line in
  [`esphome/config.yaml`](../esphome/config.yaml) drops the whole HTTP surface (a
  smaller firmware without port 80): the monitor - the sensors, the Home
  Assistant API and the local pre-alarm - does not need it.

## What does not work: AP and station at the same time

Stock ESPHome cannot run the SoftAP and the station simultaneously. The AP is a
**fallback**: `WiFiComponent::check_connecting_finished()` disables it
(`wifi_mode_({}, false)`) the moment a configured network connects, and the
fallback block in `WiFiComponent::loop()` is gated on `!this->ap_setup_`, so it
does not come back until the next boot. There is no configuration option to keep
it on - that needs a custom external component re-asserting `WIFI_AP_STA` mode
against the Wi-Fi state machine.

Practical result: while the board is on your Wi-Fi you reach it over the LAN and
Home Assistant, and while it is offline you reach it over its own AP - but never
both at once.

## Offline reliability

Two timeouts matter when the board runs without a router:

* `api: reboot_timeout:`
  ([`../esphome/packages/board.yaml`](../esphome/packages/board.yaml), 30 min in
  this project, ESPHome's own default is 15 min) reboots the board when **no Home
  Assistant client connects** within that time. Offline - exactly the case this
  mode is for - that means a reboot every 30 minutes: it re-arms the fallback AP
  but also interrupts the sensor stream briefly. Set `reboot_timeout: 0s` to
  disable it for a permanently offline board.
* `wifi: reboot_timeout` does **not** apply while an `ap:` is configured, so the
  radio will not reboot the board out from under the AP.

## Security

An open AP is only as private as its radio range. Anyone who can reach the AP can

* read every entity in the dashboard (there is no `auth:` in `packages/web.yaml`),
* open the captive portal and **rewrite the Wi-Fi credentials**,
* reach the unauthenticated `esphome` OTA platform on port 3232
  ([`../esphome/packages/board.yaml`](../esphome/packages/board.yaml)) and flash
  the board.

The `web_server` OTA path is closed with `ota: false`, but the captive portal and
the OTA platform are not. That is the price of the password-less AP the offline
mode asks for; if the room is shared, give the AP a password after all
(`password:` under `ap:`) or add `auth:` to `web_server` (the dashboard then asks
for a username/password while the Wi-Fi association stays open).

## See also

* [`getting_started.md`](getting_started.md) - secrets, substitutions, packages.
* [`home_assistant_alerts.md`](home_assistant_alerts.md) - the entities the
  dashboard shows, and the thresholds they follow.
* [`troubleshooting.md`](troubleshooting.md) - when the AP does not appear.

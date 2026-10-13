# Power Distribution console

The Power Distribution header and the battery cards of the energy dashboard, as Tessera tiles. They look like the
Lovelace cards `pd-unified-header-card` and `pd-battery-management-card` as far as a screen can: same content, same
order, same tap actions. Colours are Tessera's theme roles and tones, text is Tessera's fonts.

## Set up

1. Ask Home Assistant for the two template sensors under Data (the contract is exact).
2. Add the plugin to a screen in Tessera; the screen builds once.
3. In Layout, place **PD header** and four **PD battery** tiles; pick the sensor and the battery in the inspector.

## Tiles

- **PD header** (4x2 and up): title, Grid / Target / Total, the mode pill, the Burst pill, the story and the glance
  line. Tap it while Burst is on to turn Burst off.
- **PD battery** (3x2 and up): one battery per tile (Delta Pro 3, Stream Ultra X, Stream AC Pro, Stream Max): the
  eligible and online pills, SoC, remaining kWh, charge or cap, set and act, and the bars. The outline is the glow.
  Tap it to flip the battery's eligibility.

## Data

Tessera tiles read one entity with flat attributes (a text is cut at 48 bytes, a tile gets 16 attributes), so the
tiles read two template sensors, one tile each. Pick them in the inspector.

`sensor.pd_screen_header`, state as `sensor.pd_header_json`, attributes: `mode`, `tone` (normal, accent, busy, alert,
good), `ctrl`, `grid_w`, `total_w`, `tgt_w`, `burst`, `story1`, `story2`, `dp3_w`, `ux_w`, `ac_w`.

`sensor.pd_screen_batteries`: for `dp3`, `ultra`, `ac`, `max` a list of 15 numbers: soc, kwh, charge, cap, set, act,
online (0/1), eligible (0/1), bar_soc, bar_set, bar_act, bar_solar (-1 without solar), glow (0 off, 1 charge,
2 active, 3 master, 4 equal), ble_online (0/1), iob_online (0/1); `conn_dp3`, `conn_ultra`, `conn_ac`, `conn_max`
(text); `glow_tone`.

## Home Assistant actions

`input_boolean.turn_on` and `turn_off` on the `pd_batt_*_eligible` booleans and `pd_predictive_boost_active_boolean`.
The screen must be allowed to perform actions.

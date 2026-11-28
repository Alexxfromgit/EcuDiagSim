# Scenario format

A scenario is a JSON file with a list of steps that run one after the other. Load one in the
*Lessons* tab (**Run a file…**) or with `--scenario file.json`. Bundled lessons live in
`resources/scenarios/` and are also executed by the test `tst_scenarios`.

```json
{
  "name": "My lesson",
  "description": "Shown in the lesson list",
  "level": "Beginner",
  "topics": ["ECM", "Sensors"],
  "steps": [
    { "action": "ignition", "value": "on" },
    { "action": "startEngine" },
    { "action": "waitFor", "signal": "EngineRunning", "op": "==", "value": 1, "timeout": 5000 },
    { "action": "inject", "target": "ect", "fault": "short_gnd", "note": "Shown in the log before the step runs" },
    { "action": "wait", "ms": 1500 },
    { "action": "expect", "dtc": "P0117", "ecu": "ECM", "status": "confirmed" }
  ]
}
```

`level` (`Beginner`, `Intermediate` or `Advanced`) and `topics` are optional; the *Lessons* tab
shows them as tags and filters by level.

`equipment` (optional) lists the optional systems a lesson needs, by identifier: `engine_obd`,
`tpms`, `srs`, `esc`. Running the lesson builds the vehicle with the user's equipment (Settings) plus
these, so a lesson always finds what it needs; the lesson card shows them as chips. Base-car lessons
leave it out.

Any step may carry a `"note"`. A failed expectation is logged and the scenario continues; it ends
as *failed* if at least one expectation was not met.

## Driver actions

| Action | Fields |
|---|---|
| `ignition` | `value`: `"on"` / `"off"` |
| `startEngine` | holds the key in *start* until the engine runs (max. 3 s) |
| `warmUp` | sets engine and transmission to operating temperature |
| `selector` | `value`: `"P"`, `"R"`, `"N"`, `"D"` |
| `throttle`, `brake` | `value`: 0…100 % |
| `headlamps` | `value`: `"off"`, `"park"`, `"low"` |
| `parkBrake` | `value`: true / false |
| `tyre` | `wheel`: `FL`, `FR`, `RL`, `RR`; `value`: cold pressure in bar (placard 2.4) |
| `crash` | frontal impact at the current speed; the airbag module fires above 20 km/h delta-v |
| `steering` | `value`: steering wheel angle in degrees, + = left, ±540 lock to lock (ratio 15:1) |
| `surface` | `value`: `"dry"` (μ 1.1), `"wet"` (0.7), `"snow"` (0.3), `"ice"` (0.12) |
| `replaceAirbags` | workshop: new airbags in place of fired ones (the module keeps its crash record) |
| `fuel` | `value`: litres in the tank (0…55) — empty it, or refuel |
| `log` | `text`: a line for the lesson log, nothing else happens |
| `input` | `name`: `highBeam`, `turnLeft`, `turnRight`, `hazard`, `doorLock`, `doorFL`, `doorFR`, `doorRL`, `doorRR`, `seatBelt` (driver buckled, airbag system); `value`: true / false. Left and right turn signals exclude each other, as on the stalk. |

## Time

| Action | Fields |
|---|---|
| `wait` | `ms` of simulation time |
| `waitFor` | `signal`, `op` (`==` `!=` `<` `<=` `>` `>=`), `value`, `timeout` in ms (default 10000). Fails on timeout. |

Signal names are the DBC signal names; the value is the latest one seen on the bus. A signal
that has not been received for more than 1000 ms counts as missing (its sender went silent), so the
check fails; set `"maxAgeMs"` on the step to allow older values.

## Faults

| Action | Fields |
|---|---|
| `inject` | `target`, `fault`, optional `p1`, `p2` |
| `clearFault` | `target`, or `"all"` |

An `unconfigured` fault ends when the scan tool configures the part (coding written, routine
completed); `clearFault` instead puts the old, configured part back.

| Target | Faults |
|---|---|
| `wss_fl` `wss_fr` `wss_rl` `wss_rr` `ckp` `ect` `tps` `tft` | `open`, `short_gnd`, `short_bat`, `stuck` (p1 = value), `offset` (p1), `gain` (p1), `noise` (p1 = sigma), `intermittent` (p1 = period ms, p2 = open share 0…1), `erratic` |
| `eop` (oil pressure) `fls` (fuel level sender) | the sensor faults above |
| `maf` `o2` | the sensor faults above — engine OBD pack only |
| `intake_leak` | `failure` (a vacuum leak behind the air flow meter) — engine OBD pack only |
| `ignition_coils` | `misfire`, `p1` = cylinder 1…4 (default 3) — engine OBD pack only |
| `tyres` | `leak`, `p1` = tyre 1 FL, 2 FR, 3 RL, 4 RR (default 1), 0.6 bar a minute; `clearFault` plugs and inflates |
| `tpms_fl` `tpms_fr` `tpms_rl` `tpms_rr` | `failure` (flat battery: silent), `unconfigured` (new sensor, new ID) — TPMS only |
| `airbag_driver` `airbag_passenger` | `open`, `short_gnd` (squib circuit) — airbag system only |
| `belt_switch` | `open` (buckle switch circuit) — airbag system only |
| `sas` `yaw` | the sensor faults above; `sas` also `unconfigured` (after a wheel alignment: 15° off until calibrated) — ESC only |
| `srs` | `power_loss`, `bus_off`, `unconfigured` (new module: blank coding, no crash record) — airbag system only |
| `alternator` `cooling` `bulb_low_left` `oil_pump` `park_brake_switch` | `failure` (the switch sticks closed) |
| `throttle_body` | `unconfigured` — new part, adaptation not learned (run routine 0x0201) |
| `ecm` `tcm` `abs` `bcm` `ipc` | `power_loss`, `bus_off` |
| `abs` `bcm` | also `unconfigured` — replaced module with blank coding (write DID 0x0100) |
| `abs_e2e` | `frozen_counter`, `bad_checksum` |
| `can_bus` | `bus_short` |

## Expectations

| Form | Meaning |
|---|---|
| `{ "action": "expect", "signal": "AbsLampReq", "op": "==", "value": 1 }` | CAN signal, received within the last `maxAgeMs` (default 1000) |
| `{ "action": "expect", "dtc": "C0035-13", "ecu": "ABS", "status": "confirmed" }` | fault memory, read directly from the ECU. Without the failure type byte any `C0035-xx` matches. `status`: `confirmed`, `pending`, `testFailed`, `warning`, `stored` (pending or confirmed), `notConfirmed`, `absent` |
| `{ "action": "expect", "lamp": "abs", "value": true }` | cluster lamp: `mil`, `abs`, `tcs`, `battery`, `coolant`, `trans`, `lampFault`, `door`, `lowBeam`, `highBeam`, `oil`, `lowFuel`, `parkBrake` (also while flashing); `mil` counts a flashing MIL as on, `milFlashing` checks the flashing alone; `tpms` and `tpmsFlashing` the same way; `airbag`, `seatBelt` (also while flashing); `esc` (on or flashing), `escFlashing` |

## Scan tool

| Action | Fields |
|---|---|
| `uds` | `ecu`: `ECM`, `TCM`, `ABS`, `BCM`, `SRS` (airbag system) or `ALL` (functional, 0x7DF); `request`: hex bytes. Checks, all optional: `expect` (a response starts with these bytes; a list such as `["62 01 00 01", "62 01 00 09"]` accepts any one of them), `expectContains`, `expectNotContains`, `expectNoResponse`. A check that is not valid hex fails the step. |
| `unlock` | `ecu` — extended session plus security access with the correct key |

OBD-II modes go through `uds` as well: `{ "action": "uds", "ecu": "ALL", "request": "03" }`.

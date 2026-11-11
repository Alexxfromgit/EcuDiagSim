# Scenario format

A scenario is a JSON file with a list of steps that run one after the other. Load one in the
*Lessons* tab (**Run a file…**) or with `--scenario file.json`. Bundled lessons live in
`resources/scenarios/` and are also executed by the test `tst_scenarios`.

```json
{
  "name": "My lesson",
  "description": "Shown in the lesson list",
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
| `input` | `name`: `highBeam`, `turnLeft`, `turnRight`, `hazard`, `doorLock`; `value`: true / false |

## Time

| Action | Fields |
|---|---|
| `wait` | `ms` of simulation time |
| `waitFor` | `signal`, `op` (`==` `!=` `<` `<=` `>` `>=`), `value`, `timeout` in ms (default 10000). Fails on timeout. |

Signal names are the DBC signal names; the value is the latest one seen on the bus.

## Faults

| Action | Fields |
|---|---|
| `inject` | `target`, `fault`, optional `p1`, `p2` |
| `clearFault` | `target`, or `"all"` |

| Target | Faults |
|---|---|
| `wss_fl` `wss_fr` `wss_rl` `wss_rr` `ckp` `ect` `tps` `tft` | `open`, `short_gnd`, `short_bat`, `stuck` (p1 = value), `offset` (p1), `gain` (p1), `noise` (p1 = sigma), `intermittent` (p1 = period ms, p2 = open share 0…1), `erratic` |
| `alternator` `cooling` `bulb_low_left` | `failure` |
| `ecm` `tcm` `abs` `bcm` `ipc` | `power_loss`, `bus_off` |
| `abs_e2e` | `frozen_counter`, `bad_checksum` |
| `can_bus` | `bus_short` |

## Expectations

| Form | Meaning |
|---|---|
| `{ "action": "expect", "signal": "AbsLampReq", "op": "==", "value": 1 }` | CAN signal |
| `{ "action": "expect", "dtc": "C0035-13", "ecu": "ABS", "status": "confirmed" }` | fault memory, read directly from the ECU. Without the failure type byte any `C0035-xx` matches. `status`: `confirmed`, `pending`, `testFailed`, `warning`, `stored` (pending or confirmed), `absent` |
| `{ "action": "expect", "lamp": "abs", "value": true }` | cluster lamp: `mil`, `abs`, `tcs`, `battery`, `coolant`, `trans`, `lampFault` |

## Scan tool

| Action | Fields |
|---|---|
| `uds` | `ecu`: `ECM`, `TCM`, `ABS`, `BCM` or `ALL` (functional, 0x7DF); `request`: hex bytes. Checks, all optional: `expect` (a response starts with these bytes), `expectContains`, `expectNotContains`, `expectNoResponse` |
| `unlock` | `ecu` — extended session plus security access with the correct key |

OBD-II modes go through `uds` as well: `{ "action": "uds", "ecu": "ALL", "request": "03" }`.

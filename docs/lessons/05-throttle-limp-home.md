# Lesson 5 — Throttle position sensor, limp home

**Goal:** see a control unit protect the car when it can no longer trust a sensor: a substitute
strategy (limp home) instead of a breakdown.

## Steps

1. Ignition on, start the engine, selector **D**, accelerator to 60 %.
2. Click **TPS** in the car → *Short to ground* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| Sensor table | Throttle: ECU measures 0 %, pin 0.00 V, while the real plate is open |
| `100 ECM_Engine` | `ThrottlePos` drops to 25 % although `PedalPos` stays at 60 % |
| `300 ECM_Status` | `EngineLimp` = 1 |
| Cluster | **CHECK** (MIL) on; the car still drives, but accelerates slowly |

A potentiometer has a pull-down in the ECM, so a short to ground and an open circuit both read 0 V:
**P0122** (*circuit low*). Above 4.8 V it would be **P0123** (*circuit high*).

## With the scan tool

- UDS → **ECM** → raw request `22 10 02`: the sensor voltage in millivolts (`00 00`).
- OBD-II → **Read codes**: P0122, visible to any generic scan tool.

## Repair

Limp home ends as soon as the signal is plausible again — the driver gets full power back at once.
The MIL needs three fault-free trips, or a **Clear DTCs**.

## Try next

- Inject *Offset* with 30 instead: no electrical fault, but measured and commanded angle disagree —
  **P0121**, and it is a two-trip code (lesson 13).
- *Short to battery* gives P0123. Which DTC does *Open circuit* give, and why?

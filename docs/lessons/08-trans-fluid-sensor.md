# Lesson 8 — Transmission fluid sensor, MIL request over CAN

**Goal:** follow a lamp request from one control unit to another, and see why the ECM stores a
code (P0700) for a fault in the transmission.

## Steps

1. Ignition on, start the engine, let the fluid reach operating temperature.
2. Click **TFT** in the car → *Short to battery* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| Sensor table | Trans. fluid: pin 5.00 V (the input clamps battery voltage at 5 V), the TCM measures −40 °C |
| `1A0 TCM_Status` | `TransTemp` = 80 (substitute value), `TcmMilReq` = 1 |
| TCM | **P0713** (*circuit high*) |
| ECM | **P0700** (*transmission control system, MIL request*) |
| Cluster | **CHECK** on, **TRANS** off — shifting goes on with the substitute temperature |

Only the ECM drives the MIL. Every other emission relevant module asks for it over CAN, and the ECM
stores P0700 as a pointer: "look in the TCM".

## With the scan tool

- OBD-II → **Read codes**: one functional request, two answers — TCM `07 13`, ECM `07 00`.
- UDS → **TCM** → raw `22 20 03`: pin voltage `13 88` = 5000 mV.

## Repair

Repair the harness, then OBD **Clear codes** (Mode 04): one functional request clears both modules,
and the MIL goes out.

## Try next

- Clear only the ECM with UDS while the fault is still there. How long until P0700 returns?
- Inject *Erratic signal*: which DTC does the TCM store now?

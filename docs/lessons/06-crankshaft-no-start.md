# Lesson 6 — Crankshaft position sensor, stall and no start

**Goal:** understand why one small sensor can stop a healthy engine, and how live data separates
"does not crank" from "cranks but does not fire".

## Steps

1. Ignition on, start the engine and let it idle.
2. Click **CKP** in the car → *Open circuit* → **Inject fault**.
3. Press **Start engine** again.

## What to observe

| Time | Event |
|---|---|
| immediately | the ECM sees no pulses: no injection, no ignition timing |
| ~0.1 s | **P0335** confirmed (10 × 10 ms debounce), MIL on |
| ~0.2 s | the engine stalls, `EngineRunning` = 0; the charge lamp lights because the alternator stopped too |
| restart | the starter turns the engine at ~250 rpm — but the ECM still reports 0 rpm and never fires |

The sensor table makes the point: *real* 250 rpm while cranking, *ECU measures* 0 rpm.

## With the scan tool

- UDS → **ECM** → raw `22 F4 0C` (OBD PID 0x0C as a UDS identifier): `00 00` = 0 rpm while cranking.
  In the workshop this is the first check on a cranks-but-no-start car.
- **Read DTCs**: P0335, with a snapshot taken when the engine died.

## Repair

Plug the sensor back in: the engine starts at the first attempt. Clear the DTC to turn off the MIL.

## Try next

- Inject *Intermittent contact* on the CKP while driving: the engine cuts out and catches again.
- Unplug the CKP with the ignition on but the engine off. Is P0335 stored before you try to start?

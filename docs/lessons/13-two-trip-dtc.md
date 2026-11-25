# Lesson 13 — Two-trip DTC: pending versus confirmed

**Goal:** understand operation cycles, pending and confirmed DTCs, and why OBD Mode 03 and Mode 07
can show different codes.

## Background

Some monitors are allowed to fail once without consequences. **P0121** (throttle position
range/performance) needs a failure in two operation cycles (ignition on → off) before it is
*confirmed* and the MIL comes on. After the first failure it is only *pending*.

## Steps

1. Ignition on, start the engine.
2. Click **TPS** → *Offset*, value `30` → **Inject fault**. The sensor reads 30 % more than the real
   throttle angle; measured and commanded angle disagree.
3. Read the codes, then switch the ignition **off** and **on** and start the engine again.

## What to observe

| | Trip 1 | Trip 2 |
|---|---|---|
| UDS status of P0121 | pending, test failed | pending, **confirmed**, warning indicator |
| OBD Mode 07 (pending) | P0121 | P0121 |
| OBD Mode 03 (stored) | — | P0121 |
| MIL | off | **on** |

An emissions inspection reads Mode 03: after the first trip the car would pass, although the fault
is already known to the ECM.

## With the scan tool

- OBD-II → **Read codes** sends Mode 03 and Mode 07: the code appears as *pending* only.
- UDS → **ECM** → **Read DTCs**: hover a code for its status bits.

## Repair

New throttle body, OBD **Clear codes**. Without a clear the MIL would go out after three trips
without the fault, and the DTC would age out after 40.

## Try next

- Repair after trip 1 and drive another trip: the pending code disappears on its own.
- Which other DTCs in the [catalog](../dtc-catalog.md) need two trips?

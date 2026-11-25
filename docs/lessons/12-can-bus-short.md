# Lesson 12 — CAN bus short: every module goes silent

**Goal:** recognise a bus fault from the pattern of symptoms and U-codes, instead of replacing the
module that happens to show the first code.

## Steps

1. Ignition on, start the engine.
2. Click the CAN bus wiring (the **CAN 500k** tag) in the car → *CAN wiring short* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| Car view | the bus wiring blinks red, no frames travel along it |
| Trace | nothing new: CAN-H shorted to CAN-L, no frame gets through |
| Cluster | **CHECK**, **ABS**, **TCS** and **TRANS** on — fail-safe for silent senders; **BATT** too, because the cluster no longer hears "engine running". The BCM tell-tales (turn signals, LOW, HIGH, DOOR, BULB) go dark instead of freezing |
| Engine | keeps running: the ECM needs its sensors, not the bus |
| Scan tool | no answer from any module, not even to a functional request |

## After the repair

| Module | U-codes |
|---|---|
| ECM | U0101 (TCM), U0121 (ABS), U0140 (BCM) |
| TCM | U0100 (ECM), U0121 (ABS) |
| ABS | U0100 (ECM), U0140 (BCM) |
| BCM | U0100 (ECM), U0101 (TCM), U0121 (ABS) |

Everybody lost everybody: that points at the wiring, not at one node. Compare lesson 4, where only
the ABS was missing and every U-code named it.

One knock-on fault keeps the MIL on: without `ABS_Status` the TCM had no vehicle speed (**P0500**)
and asked the ECM for the lamp (**P0700**).

## With the scan tool

UDS raw request `14 FF FF FF` with **to 0x7DF** ticked clears all modules at once.

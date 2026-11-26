# Lesson 25 — New TPMS sensor: the relearn routine

*Needs **Tyre pressure monitoring**; the lesson fits it for its run.*

**Goal:** understand why a working part can set a "missing" DTC, and fix it with a routine.

## The situation

New tyres often get new valve sensors. The new sensor transmits perfectly — but with its own ID. The
BCM listens for the four IDs it has learned; the right front one never comes. After 10 s with the
ignition on: **C0755-87** (right front tire pressure sensor, *missing message*).

## Steps

1. Ignition on.
2. Faults → *Components* → **TPMS sensor FR** → *Replaced, not configured* (or **TP** at the front
   right wheel).

## What to observe

| Where | What |
|---|---|
| BCM | **C0755-87** |
| `360 BCM_Tpms` | `TpmsLampReq` = 2 (system fault), `TpmsSensorValid` = 13 (FR missing) |
| BCM DID `0314` | `01 1A 5C 4B 22` — one ID heard that belongs to no wheel: the new sensor |
| BCM DID `0311` | `1A 2B 3C 22` — the old ID the BCM still expects |
| Cluster, next ignition on | **TPMS** flashes for a minute, then steady |

The flashing is the FMVSS 138 *malfunction indicator*: a driver must be able to tell "the system
cannot watch your tyres" from "a tyre is low".

## The relearn

Scan tool → UDS → **BCM** → **Extended session** → *TPMS sensor relearn* **Start**. A real tool
triggers each sensor with a low-frequency pulse, wheel by wheel, so the BCM knows the position of
every ID. After 2 s **Result**: `71 03 03 01 02 0F` — completed, all four positions learned.

Clear the DTC: the lamp goes out.

## Try next

- Start the relearn while driving: `7F 31 22` — the car must stand still.
- Lesson 26: the same DTC with a different cause.

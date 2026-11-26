# Lesson 28 — Crash: deployment and the crash record

*Needs the **Airbag system (SRS)**; the lesson fits it for its run.*

**Goal:** follow one event through the whole car network, and understand why some data can never
be erased.

## Steps

1. Belt buckled, doors locked, engine running.
2. Drive at about 50 km/h, then press **Crash!** in Driver controls.

Below 20 km/h of delta-v (a parking bump) nothing fires.

## What happens

| Who | What |
|---|---|
| SRS | fires both airbags (white cushions in the car view), stores the crash record, sets **B0052** *deployment commanded*, sends `CrashDetected` on `370 SRS_Status` |
| ECM | reads `CrashDetected` and stops the fuel pump: the engine stops and stays off until the key is turned off |
| BCM | unlocks every door for the rescuers and switches the hazard lights on |
| Cluster | **AIRBAG** on |

These are three control units reacting to one message — a good reason to keep `SRS_Status` on the
bus even though only the cluster needs the lamp.

## The crash record

DID `5010`: `01 32 01` — stored, delta-v 50 km/h, driver belted. **Clear DTCs** answers
`7F 14 22` (*conditionsNotCorrect*): investigators and insurers rely on this record, so no tool can
erase it.

## The repair

1. **Replace airbags** (Driver controls): the squib circuits measure 2.2 Ω again, but the module
   still holds its record and still refuses to clear.
2. A new module: Faults → **SRS** → *Replaced, not configured*. No record — but **U3000-55**, the
   coding is blank.
3. **Extended session**, **Unlock**, write coding `03` (passenger airbag + belt reminder) to DID `0100`.
4. **Clear DTCs**: `54`. The lamp goes out.

The next ignition cycle starts normally: the ECM and BCM only react to `CrashDetected` received in
the *current* cycle, not to the last value they heard before the key was turned off.

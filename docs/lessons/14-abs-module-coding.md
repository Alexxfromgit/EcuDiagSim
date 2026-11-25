# Lesson 14 — Replacement ABS module: variant coding

**Goal:** understand why a new control unit has to be *coded* before it works, and write the
coding with WriteDataByIdentifier (UDS 0x2E).

One hardware part number fits many car variants. What makes the module fit *this* car is its
variant coding: here the flags (bit 0: stability control fitted) and the rolling circumference of
the tyres in millimetres. The ABS counts wheel sensor pulses and needs the circumference to turn
them into a speed.

## Steps

1. Ignition on, start the engine.
2. Faults → *Control units* → **ABS** → *Replaced, not configured* → **Inject fault**
   (or right-click the ABS box in the car).

## What to observe

| Where | What |
|---|---|
| ABS | **U3000-55** — control module not configured (FTB 0x55 = *not configured*) |
| Cluster | **ABS** and **TCS** lamps on, speedometer at 0 |
| `0B1 ABS_Status` | `VehicleSpeedValid` = 0 |
| TCM | **P0500** a moment later: no vehicle speed for the shift strategy — the MIL follows via P0700 |

## Write the coding

1. Scan tool → UDS → **ABS** → *Coding* **Read**: `62 01 00 FF FF FF` — blank.
2. **Write** `00 07 9C` straight away: `7F 2E 7F`, the default session does not allow it.
3. **Extended session**, **Write** again: `7F 2E 33`, *securityAccessDenied*.
4. **Unlock**, then **Write** `00 07 9C`: `6E 01 00`. Flags `00`, circumference `0x079C` = 1948 mm.

Try `00 09 9C` (2460 mm) first: the ABS answers `7F 2E 31` (*requestOutOfRange*) — the server
checks the value, not only the format.

The speed returns at once, but the DTCs stay stored and keep the ABS lamp lit until they are
cleared: **Clear DTCs · 14** on the ABS, **Clear codes · 04** for the knock-on P0500/P0700.

## Try next

- Code a wrong tyre: `00 06 D6` (1750 mm). Nothing sets a DTC, but the speedometer reads 10 % low
  and the TCM shifts late — compare `WheelSpeed_FL` with the *true speed* in Driver controls.
- **Repair** in the Faults tab puts the old, coded module back.

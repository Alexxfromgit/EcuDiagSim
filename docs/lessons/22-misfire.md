# Lesson 22 — Misfire on cylinder 3: the flashing MIL

*Needs the **Engine OBD pack**; the lesson fits it for its run.*

**Goal:** understand misfire detection, the one OBD fault that makes the MIL flash, and use the
per-cylinder data to find the cylinder.

## How the ECM detects a misfire

Each combustion speeds the crankshaft up a little. The ECM times the teeth of the crank wheel: when
one cylinder's power stroke is missing, that segment is slow. It counts misfires per cylinder over
windows of 200 revolutions (400 firings in a four-cylinder engine).

- More than 5 % of a cylinder's firings missing → **P030x** for that cylinder (**P0300** if several).
- More than 8 % of all firings missing → the unburned fuel would overheat the catalytic converter:
  the MIL **flashes**. The driver must slow down or stop.

The simulator models the firing order 1-3-4-2 and hands the ECM the per-cylinder result that a real
ECM derives from the tooth timing (only while the crankshaft sensor circuit is intact).

## Steps

1. Ignition on, start the engine.
2. Faults → *Components* → **Ignition coils** → *Misfire (cylinder)*, value `3` → **Inject fault**
   (or **COIL** in the car).
3. Hold some throttle in P (≈ 2300 rpm): at idle a window takes 15 s, at 2300 rpm 5 s.

## What to observe

| Where | What |
|---|---|
| Cluster | **CHECK** flashes |
| `300 ECM_Status` | `MilFlash` = 1; `320 ECM_Mixture` `MisfireCyl` = 4 (bit 2 = cylinder 3) |
| ECM | **P0303**, confirmed at once |
| DID `1010` | misfire counters per cylinder in the last window: `00 00 64 00` (100 of 100) |
| OBD Mode 02 | the freeze frame was stored by P0303: rpm, load, coolant, trims at that moment |
| Fuel trims | positive: the sensor sees the oxygen of the unburned charge |

## After the repair

The next clean window stops the flashing; the MIL stays on steadily because P0303 is confirmed —
clear it with Mode 04.

## Try next

- Swap the coil in a real workshop: if the misfire moves to the other cylinder, it was the coil. Here,
  inject cylinder 1 instead of 3 and see the counters follow.
- Unplug the crankshaft sensor while misfiring: the misfire monitor cannot run without it.

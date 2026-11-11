# Lesson 3 — Alternator failure, low system voltage

**Goal:** see one root cause produce DTCs in several control units, and learn to look for the
common factor instead of chasing each code.

## Steps

1. Ignition on, start the engine. `BatteryVoltage` on `350 BCM_Status` is about 14.2 V — the
   alternator is charging. The **BATT** lamp is off.
2. Click **ALT** in the car → *Component failure* → **Inject fault**.
3. Set the speed to 5× and watch the voltage.

## What to observe

| Time | Event |
|---|---|
| immediately | voltage drops to battery level, about 12.6 V |
| ~10 s | below 12.0 V the cluster lights **BATT** |
| ~27 s | below 11.0 V |
| 3 s later | ECM stores **P0562**, BCM stores **B1318-16**; the MIL lights |

Two ECUs, two different codes, one cause. Each measures its own supply and has no idea why it is low.

## With the scan tool

- OBD-II → **Live data**: *Control module voltage* from both ECM and TCM.
- UDS → **BCM** → **Read DTCs**: `93 18 16` = B1318-16. The BCM is not an OBD ECU, so the generic
  Mode 03 does not show this code — a reason why workshop tools speak UDS to every module.
- Click a DTC for its snapshot: the voltage at detection.

## Repair

Repair the alternator: 14 V at once, **BATT** goes out. Both DTCs remain stored as history
(status `0xAE`, later `0x28` after the next ignition cycle), and the MIL stays on until cleared.

## Try next

- Leave the alternator broken and switch the ignition off and on. Can you still start the engine?
- Read `19 06` extended data for P0562 after a few failures and repairs: the occurrence counter.
- Add *Power supply lost* on the ABS as well and compare which DTCs are caused by which fault.

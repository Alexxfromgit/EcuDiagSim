# Lesson 16 — Body functions by coding: daytime lights and auto-lock

**Goal:** switch functions on and off through the variant coding, the everyday job behind
"retrofits" and "activations".

The BCM software contains more functions than a given car uses. Its coding (DID `0x0100`, one
byte) has a bit per function:

| Bit | Function |
|---|---|
| 0 (`01`) | lamp monitoring — current check of the low beams, **B1601** and the **BULB** tell-tale |
| 1 (`02`) | auto-lock — locks the doors once per trip above 15 km/h; an opened door or the central locking switch ends it |
| 2 (`04`) | daytime running lights — the position lamps light while the engine runs |
| 3 (`08`) | tyre-pressure monitoring — only on a car with TPMS fitted |

## Steps

1. Ignition on, start the engine.
2. Scan tool → UDS → **BCM** → *Coding* **Read**: `62 01 00 01`, lamp monitoring only
   (`62 01 00 09` if TPMS is fitted in Settings: bit 3 is set as well).
3. **Extended session**, **Unlock**, enter `07` and **Write**: `6E 01 00`. With TPMS fitted,
   write `0F` instead to keep tyre-pressure monitoring.

## What to observe

| Where | What |
|---|---|
| Car view | the position lamps light at once |
| DID `0x4002` | bit 2 (park lamps) set |
| Driver controls | drive off with the doors unlocked: above 15 km/h the lock chip turns **LOCKED** |
| `350 BCM_Status` | `DoorLock` = 1 |

## Try next

- Write `10`: `7F 2E 31` — the BCM checks that only known bits are set. Without TPMS, `08` is
  refused too: the BCM only accepts the functions the car has.
- Write `00` (no lamp monitoring), break the left low beam bulb: no B1601, no **BULB** lamp. The
  coding decides which monitors run.
- Inject *Replaced, not configured* on the BCM: the coding reads `FF`, **U3000-55** is set and
  every coded function is off until you write a valid coding.

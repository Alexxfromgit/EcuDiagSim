# Lesson 26 — Silent TPMS sensor: flat battery

*Needs **Tyre pressure monitoring**; the lesson fits it for its run.*

**Goal:** tell two causes of the same DTC apart, and enter a sensor ID by hand with
WriteDataByIdentifier.

TPMS sensors have a sealed battery that lasts 7–10 years. When it is flat, the sensor goes silent.

## Steps

1. Ignition on.
2. Faults → **TPMS sensor RL** → *Component failure* (the battery).

## Same code, different cause

| | new sensor, not learned (lesson 25) | flat battery |
|---|---|---|
| DTC | C07xx-87 missing message | C07xx-87 missing message |
| DID `0314` unknown IDs | the new ID | `00` — nothing heard |
| Relearn | completes, `0F` | **fails** after 30 s: `71 03 03 01 04 0B` — FL, FR, RR learned, RL not |
| Fix | relearn | a new sensor |

## Typing the ID in

The new sensor's ID is printed on its box and on the sensor. Instead of a relearn, write it into the
position's identifier — this needs security access:

1. Faults → **TPMS sensor RL** → *Replaced, not configured* (the new sensor is fitted).
2. Scan tool → BCM → **Extended session**, **Unlock**.
3. Raw request `2E 03 12 1A 5C 4B 33` → `6E 03 12`. DIDs `0310`…`0313` are FL, FR, RL, RR.

`2E 03 12 00 00 00 00` is refused with `7F 2E 31`: no sensor has ID 0.

Within a second the BCM hears the rear left sensor again; clear the DTC and the lamp goes out.

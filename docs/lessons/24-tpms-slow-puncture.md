# Lesson 24 — Slow puncture: the TPMS warning

*Needs **Tyre pressure monitoring** (Settings → Vehicle equipment); the lesson fits it for its run.*

**Goal:** see how a direct TPMS works and why a low tyre is a warning, not a DTC.

Each wheel has a battery-powered sensor on the valve. It sends its ID, the pressure and the
temperature by radio, once a second here (real sensors: every 15–60 s while driving). The BCM knows
which ID belongs to which wheel and puts the four pressures on `360 BCM_Tpms`. Below **75 % of the
placard pressure** (2.4 bar on the door sticker → 1.8 bar) it requests the TPMS lamp — the US rule
FMVSS 138.

## Steps

1. Ignition on, start the engine.
2. Faults → *Components* → **Tyres (slow puncture)** → value `2` (front right) → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| Car view | the front right tyre outline turns amber, then red, and bulges |
| `360 BCM_Tpms` | `TirePressure_FR` falls about 0.6 bar a minute; `TpmsLampReq` = 1 below 1.8 bar |
| Cluster | **TPMS** steady |
| BCM DIDs `4020…4023` | the four pressures in 0.02 bar |
| Fault memory | nothing: the car is fine, the tyre is not |

## Warm and cold

Tyre pressure rises as the tyre warms up (drive a few minutes at 100 km/h: +0.1…0.2 bar). The BCM
compares the pressure converted back to 20 °C, because the placard value is a cold pressure.

## Without TPMS

Run the same fault on a car without the equipment: no lamp, nothing on the bus. Only the ABS could
notice — a soft tyre rolls on a smaller radius and turns about 1 % faster (*indirect TPMS*), far inside
its 25 % plausibility window. Compare the wheel speeds in the ABS live data.

# Lesson 19 — Oil lamp on: pump or sensor?

**Goal:** tell a real mechanical fault from a sensor fault when both light the same lamp.

The oil pressure follows engine speed (about 1.4 bar at warm idle, over 3 bar at 3000 rpm) and drops
as the oil gets hot and thin. The ECM measures it with a 0.5–4.5 V sensor (0–10 bar) and requests the
red **OIL** lamp below 0.4 bar. That includes the engine stopped: with the ignition on and no
pressure, the lamp is on, like in every car.

## Part A — the pump fails

Faults → *Components* → **Oil pump** → *Component failure* (or **OIL** in the car).

| Where | What |
|---|---|
| Car view sensor table | real ≈ 0.1 bar, ECU measures the same: the sensor tells the truth |
| ECM | **P0524** — engine oil pressure too low (engine running for more than 3 s, then 2 s) |
| Cluster | **OIL** on, **no** MIL: not an emission fault, the red lamp alone means stop |

## Part B — the sensor is unplugged

Repair the pump, clear the codes, then **EOP** → *Open circuit*.

| Where | What |
|---|---|
| Car view sensor table | real ≈ 1.4 bar, ECU measures 0 bar |
| DID `1006` | 0 V — a working sensor never goes below 0.5 V |
| ECM | **P0522** — sensor circuit low; **no** P0524, the pressure monitor needs a working sensor |
| Cluster | the same **OIL** lamp (fail-safe) |

## What to remember

Same lamp, different codes: *circuit* codes (P0522/P0523) point at the wiring and the sensor,
*performance* codes (P0524) at the engine. Before you condemn the pump, read the live value and
compare it with a mechanical gauge.

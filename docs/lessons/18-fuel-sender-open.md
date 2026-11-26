# Lesson 18 — Fuel level sender: open circuit

**Goal:** apply the pull-up circuit model from the coolant sensor lesson to a different sensor, and
see why a sender fault is emission relevant.

The fuel level sender is a float on a resistor track. The ECM feeds it through a pull-up: a full tank
gives a low voltage (about 0.5 V), an empty one a high voltage (4.5 V). A broken wire lets the pull-up
win: 5 V, "empty". The circuit is designed so a fault can never show a full tank.

## Steps

1. Ignition on, start the engine.
2. Click **FUEL** at the rear of the car → *Open circuit* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| ECM | **P0463** — fuel level sensor circuit high, after 2 s |
| DID `1007` | 5000 mV at the pin |
| `310 ECM_Fuel` | `FuelLevelValid` = 0, `FuelLevel` = 0 |
| Cluster | fuel gauge empty, **FUEL** lamp, trip computer `FUEL --`; **CHECK** (MIL) |
| Car view sensor table | real level 40 l, ECU measures 0 l |

The ECM needs the tank level for the evaporative emission (EVAP) leak test, so the code is emission
relevant and lights the MIL.

## Try next

- *Short to ground* instead: **P0462**, the pin reads 0 V — on the resistor track that would mean
  "full", so the ECM flags the value invalid as well.
- While the sender is open the ECM keeps its last good level internally: after the repair the gauge
  is back at once instead of climbing from zero.

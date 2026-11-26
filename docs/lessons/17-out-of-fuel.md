# Lesson 17 — Out of fuel: cranks, no start, no DTC

**Goal:** learn that an empty fault memory does not mean "nothing wrong", and use live data to find
a cause that no monitor reports.

## Steps

1. Ignition on, start the engine.
2. In the lesson the tank is set to 50 ml; by hand, drive until the tank is empty, or run the
   lesson.
3. Drive off at full throttle until the engine dies, then stop and try to start again.

## What to observe

| Where | What |
|---|---|
| Cluster | **FUEL** lamp below 7 l; fuel gauge empty; trip computer range 0 km |
| Driver controls | the starter turns (`STARTER`, ~250 rpm) but the engine does not fire |
| Scan tool → OBD **Read codes** | `43 00` — nothing stored |
| OBD live data | PID `2F` fuel tank level 0–1 % (the ECM damps the sender, so it lags a little) |

## Why there is no P0335

When an engine stalls, its speed falls through the low range before the signal disappears. When the
crankshaft sensor fails, the signal drops to zero at once. The ECM tells the two apart from the last
engine speed it saw with a signal, so a stall never sets P0335.

## The fuel model

Consumption follows the engine's power output. Idle burns about 1 l/h, full throttle in a low gear
more than 15 l/h, and closed throttle in gear (overrun) cuts the injectors completely — watch the
trip computer show 0.0 l/h on the overrun. The ECM reports the rate on `310 ECM_Fuel`; the cluster
turns it into l/100 km, an average and a range.

## Try next

- **Refuel** in Driver controls (engine off) and watch the gauge rise over a few seconds: the ECM
  damps the sender against fuel sloshing.
- Compare a drive at a steady 30 % throttle in 6th gear with one in 3rd (selector D, light versus
  heavy throttle): same speed, different fuel rate.

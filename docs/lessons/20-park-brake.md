# Lesson 20 — Park brake: driving off with it, and a stuck switch

**Goal:** see a warning built from a plain switch and a speed signal, and diagnose a fault that no
monitor can detect.

The hand brake acts mechanically on the rear axle (about a quarter of g). A switch under the lever
tells the BCM; the BCM sends `ParkBrake` on `350 BCM_Status` and, above 10 km/h, `ParkBrakeWarn`.
The cluster lights the red **BRAKE** tell-tale and flashes it with the warning.

## Steps

1. Ignition on, start the engine, **Park brake → Applied** in Driver controls. The rear brakes glow
   in the car view.
2. Selector **D**, 25 % throttle: the car stays put.
3. 70 % throttle: the car drags itself off slowly. Above 10 km/h **BRAKE** flashes.
4. Stop, release the park brake. Then click **PB** in the car → *Component failure* (the switch
   sticks closed).

## What to observe

| Where | What |
|---|---|
| Cluster | **BRAKE** stays on with the lever released, and flashes when you drive |
| Scan tool → BCM → **Read DTCs** | nothing: a switch is either open or closed, both are valid |
| BCM DID `4004` | switch inputs: bit 0 = 1, the BCM believes the brake is applied |
| Car view | no glow at the rear brakes: the brake really is off |

Without a second source (a lever position sensor, or the brake not dragging) no ECU can know that
the switch lies. This is where live data and a look at the real thing come in.

## Try next

- Compare the time to 12 km/h at 70 % throttle with and without the park brake.
- An electric park brake would have a motor current and position feedback — and DTCs. The
  mechanical one only has a switch.

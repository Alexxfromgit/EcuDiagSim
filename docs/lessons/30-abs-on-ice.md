# Lesson 30 — Braking on ice: ABS at work

*Base car: every vehicle has ABS.*

**Goal:** see what anti-lock braking actually does, on the bus and on the road, and why an ABS lamp
matters.

## How the ABS works here

Each wheel grips at most μ × its load (dry 1.1, wet 0.7, snow 0.3, ice 0.12). A wheel whose brake
force is higher locks and slides at 80 % of that. The ABS watches every wheel speed against a
*reference speed* — the fastest trustworthy wheel, which may drop at most 1.3 g — and as soon as a
wheel slips more than 15 % it:

1. **dumps** that wheel's pressure,
2. remembers the level at which the wheel let go,
3. **rebuilds** quickly to just below it and then creeps up slowly.

So the wheel spends most of the time just under the grip limit, many cycles per second. On a dry road
even a full pedal stays under the grip: no ABS, no difference.

## Steps

1. Drive at 50 km/h, then Driver controls → *Road* → **Ice**, brake 100 %.
2. Steer while braking.

## What to observe

| Where | What |
|---|---|
| `0B1 ABS_Status` | `AbsActive` = 1 |
| Driver controls | the **ABS** chip |
| Car view | the treads keep turning (briefly pausing), the car follows the steering |
| `0B0 ABS_WheelSpeeds` | wheel speeds dip and recover several times a second |

## Without ABS

Break a wheel speed sensor first (*WSS FL → Open circuit*): ABS lamp on, the ABS is off and the
brakes work like ordinary brakes. The same braking locks the wheels: `WheelSpeed_FR` = 0 while the car
still slides, the treads stand still in the car view, and **steering does nothing** — locked front
wheels give no side force.

The point of ABS is not mainly a shorter stop (on ice the difference is small, on loose snow locked
wheels can even stop shorter): it is a car that can still be steered while braking.

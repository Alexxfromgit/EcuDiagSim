# Lesson 32 — Snow: ESC at work, then a yaw rate sensor failure

*Needs **Stability control (ESC)**; the lesson fits it for its run.*

**Goal:** see how stability control compares intention with reality, how it acts through other
control units, and how one function degrades without taking the others down.

## What the ESC compares

- **Intention:** steering angle and speed give the yaw rate the car *should* have
  (v ÷ wheelbase × tan(road wheel angle)).
- **Reality:** the yaw rate sensor measures how fast the car actually turns.

On snow (μ 0.3) the tyres cannot provide the side force for a fast bend: the car turns much less than
steered — **understeer**, it pushes wide. The ESC then brakes single wheels to slow the car and sends
`EscActive` on `0B2 ABS_Chassis`; the **ECM** reads it and cuts the torque, whatever the pedal says.

## Steps

1. Driver controls → *Road* → **Snow**, accelerate to 60 km/h.
2. Steer 120° (Driver controls → *Steering*).
3. Faults → *Sensors* → **Yaw rate sensor** → *Open circuit*, steer again, then brake hard.

## What to observe

| Where | What |
|---|---|
| Cluster | **ESC** flashes while it works |
| `0B2 ABS_Chassis` | `SteeringAngle` 120, `YawRate` well below what that asks for, `EscActive` = 1 |
| `100 ECM_Engine` | `ThrottlePos` drops to 10 % although `PedalPos` stays at 40 % |
| after the sensor fault | **C0196-11**, `EscOff` = 1, ESC lamp steady — the same bend gets no help |
| hard braking | `AbsActive` = 1, **no** ABS lamp: the ABS needs only wheel speeds |

## Degradation

A real chassis system is layered: ABS (wheel speeds) → traction control → stability control (plus
steering angle and yaw rate). A fault takes out its layer and everything above it, never what is
below. Here a yaw sensor fault leaves the ABS fully working, and only the ESC lamp comes on.

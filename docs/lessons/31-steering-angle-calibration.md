# Lesson 31 — After the wheel alignment: steering angle calibration

*Needs **Stability control (ESC)** (Settings → Vehicle equipment); the lesson fits it for its run.*

**Goal:** understand plausibility between two sensors, and a calibration routine with preconditions.

## Why a calibration

The ESC needs to know where the driver wants to go: the **steering angle sensor** in the column. It
counts from a zero point that the ABS/ESC unit has learned. A wheel alignment re-centres the steering
wheel on the road wheels — the car drives straight with the wheel straight — but the sensor still
has its old zero: now it reports 15° while the car runs straight on.

## Steps

1. Faults → *Sensors* → **Steering angle sensor** → *Replaced, not configured* (the alignment).
2. Drive straight at 40 km/h for a few seconds.

## What to observe

| Where | What |
|---|---|
| `0B2 ABS_Chassis` | `SteeringAngle` ≈ 15° while `YawRate` ≈ 0 |
| ABS | **C0460-64** *steering angle sensor, signal plausibility failure* (above 30 km/h, 3 s without yaw, more than 8°) |
| Cluster | **ESC** steady (`EscOff` = 1); **no** ABS lamp — braking is not affected |

## The calibration

Scan tool → UDS → **ABS** → **Extended session**, **Unlock** → *Steering angle calibration* **Start**
(routine `0x0401`):

- while moving: `7F 31 22` — the sensor can only learn "straight" at rest;
- at rest, wheels straight: `71 01 04 01 02` — completed. `SteeringAngle` reads 0.

Clear the codes and drive: the ESC is back.

## Try next

- Calibrate with the steering wheel turned (Driver controls → *Steering* 30°): the routine completes —
  and the ESC now believes 30° is straight ahead. A calibration is only as good as the workshop's care.
- Replace the ABS module (*Replaced, not configured* on **ABS**): **C0460-54** *missing calibration*
  in addition to the blank coding.

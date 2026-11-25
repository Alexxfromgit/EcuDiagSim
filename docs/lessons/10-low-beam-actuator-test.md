# Lesson 10 — Low beam bulb: lamp check and actuator test

**Goal:** see how a body module detects a blown bulb, and verify a repair with an actuator test
instead of the light switch.

## Steps

1. Ignition on, lights **Low**. Both headlamps light in the car view.
2. Click **BULB** in the car → *Component failure* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| Car view | the left headlamp and its beam go dark |
| BCM | output on, but no lamp current: **B1601-13** after 0.5 s |
| `350 BCM_Status` | `LampFault` = 1 → **BULB** tell-tale |

The BCM only knows about the fault while it switches the lamp on: with the lights off, the current
monitor cannot run.

## Actuator test (UDS 0x2F)

1. Repair the bulb and switch the lights **Off**.
2. Scan tool → UDS → **BCM** → *Low beam* **On**: rejected with NRC `7F`, IO control needs the
   extended session.
3. **Extended session** and **Unlock** (security access), then *Low beam* **On** again:
   `6F 40 10 03 …` — both headlamps light although the switch is off.
4. **Release** returns the output to the BCM.

## Try next

- Turn on the hazard lights and pull the BCM fuse: every exterior lamp goes dark, and the cluster
  tell-tales go out instead of freezing in their last state.
- *Door lock* (`4011`) works the same way: lock the doors from the scan tool.

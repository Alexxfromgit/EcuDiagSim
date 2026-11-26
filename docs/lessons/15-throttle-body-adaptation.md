# Lesson 15 — New throttle body: adaptation routine

**Goal:** run a routine with RoutineControl (UDS 0x31), see why it has preconditions, and read its
result.

An electronic throttle body has a mechanical stop that differs slightly from part to part. The ECM
learns where the plate is closed and stores it as an *adaptation value*. A new throttle body
arrives without one.

## Steps

1. Ignition on.
2. Faults → *Components* → **Throttle body** → *Replaced, not configured* → **Inject fault**
   (or click **THR** in the car).

## What to observe

| Where | What |
|---|---|
| ECM | **P2176** — throttle actuator control system, idle position not learned |
| Cluster | **CHECK** (MIL): P2176 is emission relevant |
| `100 ECM_Engine` / `300 ECM_Status` | with the engine running: `EngineLimp` = 1, the opening is limited to 25 % |
| DID `0x1004` | `00 …` — not learned |

## Run the adaptation

1. Scan tool → UDS → **ECM** → **Extended session**.
2. Start the engine, then *Throttle adaptation* **Start**: `7F 31 22` — *conditionsNotCorrect*.
   The ECM only learns with the engine stopped, the pedal released and the car at rest.
3. Switch the engine off (ignition off and on again), **Extended session**, then **Result**:
   `7F 31 24` — *requestSequenceError*, the routine has not run yet.
4. **Start**: `71 01 02 01 01` (status 01 = running). Watch the car view and `ThrottlePos` in the
   trace: the motor opens the plate fully and closes it again, with the engine off.
5. After 3 s **Result**: `71 03 02 01 02 xx xx` — 02 = completed, then the learned closed position
   in mV.

Clear the codes (**Clear codes · 04**) and start the engine: full power again.

## Try next

- Press the accelerator while the routine runs: it aborts (status 03).
- Unplug the throttle position sensor (TPS → *Open circuit*) and run the routine: it completes,
  but with status 04 (failed) — the plate moved, the sensor did not follow.

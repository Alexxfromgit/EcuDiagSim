# Lesson 1 — Wheel speed sensor, open circuit

**Goal:** follow one fault through every layer of the vehicle network.

Run it from the *Lessons* tab, or do it by hand as described here. Slow the simulation to 0.5× if
things happen too fast.

## Steps

1. Ignition on, start the engine, selector **D**, accelerate to about 50 km/h, then hold a light throttle.
2. *CAN trace* → **By ID**. Look at `0B0 ABS_WheelSpeeds`: four similar speeds, four valid bits,
   an alive counter that changes with every frame.
3. In the car, click the front-left **WSS** dot → *Open circuit* → **Inject fault**.

## What to observe

| Layer | Where | What happens |
|---|---|---|
| Sensor | table under the car | real 50 km/h, the ECU measures 0 — no pulses arrive |
| CAN signal | trace, `0B0` | `WheelSpeed_FL=0`, and after 200 ms `WheelSpeed_FL_Valid=0` |
| Vehicle speed | trace, `0B1` | `VehicleSpeed` stays correct and valid: the ABS averages the three good wheels |
| Fault memory | car view | the ABS box turns red and shows a DTC count |
| Lamp request | trace, `0B1` | `AbsLampReq=1`, `TcsLampReq=1` |
| Lamp | cluster | ABS and TCS light up — the cluster only shows what the ABS asks for |

The 200 ms between the wrong signal and the DTC is the **debounce time**: a monitor must see the
fault 20 times in a row (every 10 ms) before it believes it.

## With the scan tool

*Scan tool* → UDS → control unit **ABS**.

- **Read DTCs** sends `19 02 0D`. The answer `59 02 FF 40 35 13 AF` means: DTC `0x403513` =
  **C0035-13** (left front wheel speed sensor, circuit open), status `0xAF`.
- Status `0xAF` = testFailed + testFailedThisOperationCycle + pending + confirmed +
  testFailedSinceLastClear + warningIndicatorRequested. Hover the entry to see the bits.
- Click the DTC: `19 04 40 35 13 01` returns the **snapshot** — the vehicle speed and all four wheel
  speeds at the moment the fault was detected. Front left already reads 0.
- **Clear DTCs** (`14 FF FF FF`) answers `54` — and 200 ms later the DTC is back. Clearing does not
  repair anything.

## Repair

Select the sensor in the *Faults* tab and press **Repair**. `WheelSpeed_FL_Valid` returns to 1 at once,
but the DTC stays confirmed and the lamp stays on: the ABS keeps its warning for the rest of the
ignition cycle. Clear the DTCs now and both are gone for good.

## Try next

- *Short to ground* instead of open: same base code, different failure type byte (`C0035-11`).
- *Gain 0.5*: electrically perfect, so only the plausibility check can find it → `C0035-29`, after 0.5 s.
- *Stuck* while standing still: nothing happens until you drive off. Why?
- *Intermittent*: watch the status byte with **Refresh** on. Does the DTC set?

# Lesson 7 — Water pump failure, engine overheating

**Goal:** tell a driver warning from a stored fault, and read a snapshot to see the conditions at
the moment of detection.

## Steps

1. Ignition on, start the engine, selector **D**, accelerator to 30 %.
2. Click **PUMP** in the car → *Component failure* → **Inject fault**.
3. Set the speed to 5× — the engine needs about a minute to overheat.

## What to observe

| Coolant | Event |
|---|---|
| 90 °C | normal; the gauge bar in the cluster is green |
| > 112 °C | `CoolantWarnReq` on `300 ECM_Status`: red **TEMP** lamp, no DTC yet |
| > 118 °C for 3 s | **P0217** (engine over temperature) stored, MIL on |

The temperature lamp is a message to the driver: pull over now. The DTC is the record for the
workshop that the engine really overheated.

## With the scan tool

- OBD-II → **Live data**: PID 05, coolant temperature (one byte, value + 40).
- UDS → **ECM** → **Read DTCs** → click P0217: the snapshot shows speed, rpm and temperature.

## Repair

Pull over, new water pump. The temperature falls back towards 90 °C and **TEMP** goes out on its
own; P0217 stays stored until it is cleared.

## Try next

- Short the coolant sensor to ground instead: the ECM *sees* 150 °C. Why does it not light the
  temperature lamp? (Hint: P0117 and the substitute value.)
- Compare the transmission fluid temperature while overheating: it does not follow the coolant.

# Lesson 2 — Coolant temperature sensor, open circuit

**Goal:** understand how an ECU reads an analog sensor, why one sensor has several DTCs, and how the
generic OBD-II services work.

## Background

The coolant sensor is an NTC thermistor between the ECU input and ground. Inside the ECU a pull-up
resistor connects the input to 5 V. Hot coolant → low resistance → low voltage.

| Situation | Pin voltage | ECU would compute | DTC |
|---|---|---|---|
| normal, 90 °C | about 1.8 V | 90 °C | — |
| connector unplugged | 5.0 V (only the pull-up) | −40 °C | **P0118** circuit high |
| wire shorted to ground | 0 V | 150 °C | **P0117** circuit low |
| sensor stuck / jumping | in range | plausible looking | **P0116** range/performance |

The ECU decides by voltage, not by temperature: values outside 0.2…4.8 V cannot come from a working
sensor.

## Steps

1. Ignition on, start the engine. (The lesson warms the engine up instantly; by hand it takes
   about two minutes.)
2. Click **ECT** in the car → *Open circuit* → **Inject fault**.

## What to observe

- Sensor table: pin 5.00 V, the ECU measures −40 °C.
- Trace, `300 ECM_Status`: `CoolantTemp` is **80**, not −40. The ECM sends a *substitute value* so
  that the cluster and the transmission keep working sensibly.
- After 0.5 s the **CHECK** lamp (MIL) lights: `MilReq=1`.

## With the scan tool (OBD-II tab)

These requests go to the functional address 0x7DF. In the trace you see two responses: 0x7E8 (ECM)
and 0x7E9 (TCM).

- **Read codes**: Mode 03 → `43 01 01 18` = one DTC, P0118. The TCM answers `43 00`.
- **Freeze frame**: Mode 02 returns the engine data stored when the fault was detected.
- **Vehicle info**: the VIN does not fit into one CAN frame. Watch the first frame, the tester's
  flow control and the consecutive frames in the trace.
- UDS tab, ECM, raw request `22 10 01`: the sensor voltage, `13 88` = 5000 mV. Reading the raw
  voltage is how you tell a broken sensor from a real temperature problem.

## Repair

Repair the sensor. The value is right again immediately, but **the MIL stays on**. An emission
related DTC needs three fault-free ignition cycles before the lamp goes out by itself, and 40 before
the entry is erased. The quick way is **Clear codes** (Mode 04).

## Try next

- *Short to ground*: P0117 instead.
- *Erratic*: P0116 is a **two-trip** code. After the first detection it is only *pending* (Mode 07),
  the MIL stays off. Switch the ignition off and on: now it is confirmed.
- *Stuck* on a cold engine: nothing for 90 seconds, then P0116 — the engine warmed up but the signal
  did not.
- *Offset +30*: no DTC at all, yet the temperature is wrong. Which additional check could catch it?

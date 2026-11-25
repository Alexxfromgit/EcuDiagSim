# Lesson 11 — Intermittent wheel speed sensor, loose connector

**Goal:** diagnose a fault that is not there when you look: debounce, a lamp that stays on, and
the occurrence counter.

## Steps

1. Ignition on, start the engine, drive at about 40 km/h.
2. Click the rear-right **WSS** → *Intermittent contact*, period `1000` ms, open `40` % →
   **Inject fault**.

## What to observe

| Time | Event |
|---|---|
| every second | the line is open for 400 ms: `WheelSpeed_RR` drops to 0, then recovers |
| first dropout | after 200 ms of debounce **C0050-13** is confirmed and the **ABS** lamp lights |
| afterwards | the DTC toggles *test failed* on and off, the lamp stays on for this ignition cycle |

A dropout shorter than the 200 ms debounce would not set the DTC at all: try a 300 ms period.

## With the scan tool

- UDS → **ABS** → **Read DTCs** → click C0050-13: `19 06` extended data shows the occurrence
  counter, which rises with every dropout.
- **Clear DTCs**: it comes back within a second. Clearing is no repair.

## Repair

A new connector with a working locking clip. Clear the DTC: it stays away, the lamp goes out.

## Try next

- Watch the trace with filter `0B0`: how many frames carry `WheelSpeed_RR_Valid` = 0 per second?
- Set the open share to 10 %. Is the fault still detected?

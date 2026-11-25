# Lesson 9 — End-to-end protection: frozen counter and bad CRC

**Goal:** understand what the alive counter and the checksum in a safety relevant message protect
against, and why only the receiver can detect the error.

## Background

`0B0 ABS_WheelSpeeds` carries `ABS_AliveCounter` (4 bits, +1 every frame) and `ABS_Checksum`, a
CRC-8 SAE J1850 over bytes 0–6. The ECM checks both on every frame:

- **counter unchanged** → the sender's software hangs and repeats old data (stale but consistent)
- **CRC wrong** → the data was corrupted between the sender's application and the receiver

## Steps

1. Ignition on, start the engine, drive at about 20 km/h.
2. Click **E2E** in the car → *Alive counter frozen* → **Inject fault**.
3. Then change it to *Wrong checksum*.

## What to observe

| Fault | In the trace (filter `0B0`) | DTC in the ECM |
|---|---|---|
| frozen counter | the high nibble of byte 6 never changes | **U0415-82** after 10 frames |
| bad checksum | byte 7 does not match the CRC | **U0415-83** after 10 frames |

No lamp comes on: network DTCs are stored without a warning request, and the ABS itself stores
nothing — from its point of view it sends perfectly good frames.

## With the scan tool

- UDS → **ECM** → **Read DTCs**: two entries for the same base code, told apart by the failure type
  byte (`C4 15 82`, `C4 15 83`).
- UDS → **ABS** → **Read DTCs**: empty.

## Repair

Remove the fault, clear the ECM. The DTCs stay away.

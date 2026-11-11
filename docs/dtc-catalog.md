# DTC catalog

A UDS trouble code has three bytes: two for the SAE J2012 code and one **failure type byte** (FTB)
that says *how* the component failed. OBD-II services report only the first two bytes.

```
C0035-13  →  0x40 0x35 0x13
              │         └─ FTB 0x13: circuit open
              └─ bits 15..14: 00 = P (powertrain), 01 = C (chassis), 10 = B (body), 11 = U (network)
```

Detection times are deliberately short. *Trips* is the number of ignition cycles with the fault
until the DTC is confirmed and the lamp is requested; before that it is only *pending*.

## ECM — engine

| Code | Description | Detection | Trips | Reaction |
|---|---|---|---|---|
| P0116 | Coolant temperature sensor range/performance | jump > 10 °C in 100 ms, or no change 90 s after a cold start | 2 | substitute 80 °C |
| P0117 | Coolant temperature sensor circuit low | pin voltage < 0.2 V for 0.5 s | 1 | substitute 80 °C |
| P0118 | Coolant temperature sensor circuit high | pin voltage > 4.8 V for 0.5 s | 1 | substitute 80 °C |
| P0121 | Throttle position sensor range/performance | measured vs commanded angle differ > 20 % for 0.5 s | 2 | limp: throttle ≤ 25 % |
| P0122 | Throttle position sensor circuit low | pin voltage < 0.2 V for 0.2 s | 1 | limp: throttle ≤ 25 % |
| P0123 | Throttle position sensor circuit high | pin voltage > 4.8 V for 0.2 s | 1 | limp: throttle ≤ 25 % |
| P0217 | Engine over temperature | coolant > 118 °C for 3 s | 1 | TEMP lamp from 112 °C |
| P0335 | Crankshaft position sensor circuit | no speed signal while cranking or running | 1 | injection off: stall / no start |
| P0562 | System voltage low | < 11.0 V with engine running for 3 s | 1 | |
| P0700 | Transmission control system (MIL request) | TCM sets `TcmMilReq` | 1 | MIL |
| U0101-87 | Lost communication with TCM | TCM_Status missing | 1 | no lamp |
| U0121-87 | Lost communication with ABS | ABS_Status missing | 1 | no lamp |
| U0140-87 | Lost communication with BCM | BCM_Status missing | 1 | no lamp |
| U0415-82 | Invalid data from ABS: alive counter | counter of 0x0B0 unchanged for 10 frames | 1 | no lamp |
| U0415-83 | Invalid data from ABS: checksum | CRC of 0x0B0 wrong for 10 frames | 1 | no lamp |

All P-codes are emission relevant: they are visible to OBD-II, request the MIL and need three
fault-free trips before the MIL goes out by itself.

## TCM — transmission

| Code | Description | Detection | Trips | Reaction |
|---|---|---|---|---|
| P0500 | Vehicle speed signal | speed from ABS missing or invalid for 0.5 s | 1 | speed estimated from engine speed and gear |
| P0711 | Fluid temperature sensor range/performance | jump > 10 °C in 100 ms | 2 | substitute 80 °C |
| P0712 | Fluid temperature sensor circuit low | pin voltage < 0.2 V for 0.5 s | 1 | substitute 80 °C |
| P0713 | Fluid temperature sensor circuit high | pin voltage > 4.8 V for 0.5 s | 1 | substitute 80 °C |
| P0218 | Transmission fluid over temperature | > 135 °C for 3 s | 1 | limp mode (3rd gear) |
| U0100-87 | Lost communication with ECM | ECM_Engine missing | 1 | limp mode |
| U0121-87 | Lost communication with ABS | ABS_Status missing | 1 | |

## ABS — brakes

Four DTCs per wheel: C0035 (front left), C0040 (front right), C0045 (rear left), C0050 (rear right).

| FTB | Meaning | Detection |
|---|---|---|
| -11 | circuit short to ground | 0.2 s |
| -12 | circuit short to battery | 0.2 s |
| -13 | circuit open | 0.2 s |
| -29 | signal invalid | deviates > 25 % from the median of the other wheels above 10 km/h for 0.5 s |

Reaction: the wheel is flagged invalid on 0x0B0 and left out of the vehicle speed; ABS and traction
control are switched off and both lamps requested. The lamp stays on until the end of the ignition
cycle after the fault has gone (or until the DTC is cleared). Also U0100-87 and U0140-87.

## BCM — body

| Code | Description | Detection |
|---|---|---|
| B1318-16 | Battery voltage low | < 11.0 V with ignition on for 3 s |
| B1601-13 | Left low beam circuit open | no lamp current while the output is on |
| U0100-87, U0101-87, U0121-87 | Lost communication with ECM / TCM / ABS | message timeout |

B1318 and B1601 are manufacturer specific examples.

## The status byte

| Bit | Name | Meaning |
|---|---|---|
| 0 | testFailed | the fault is present right now |
| 1 | testFailedThisOperationCycle | seen at least once since ignition on |
| 2 | pendingDTC | seen in this or the previous cycle |
| 3 | confirmedDTC | stored: seen in enough cycles |
| 4 | testNotCompletedSinceLastClear | the monitor has not produced a result since the last clear |
| 5 | testFailedSinceLastClear | |
| 6 | testNotCompletedThisOperationCycle | |
| 7 | warningIndicatorRequested | this DTC wants a lamp |

Typical values: `0x50` nothing known yet, `0x00` tested and fine, `0x27` pending in the first trip
of a two-trip DTC, `0xAF` active and confirmed, `0xAE` confirmed but no longer present in this
cycle, `0x28` stored history from an earlier cycle.

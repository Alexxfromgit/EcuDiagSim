# Diagnostics reference

## Transport: ISO-TP (ISO 15765-2)

A CAN frame carries eight bytes; diagnostic messages can be up to 4095. The first nibble of each
frame says what it is:

| First byte | Frame | Layout |
|---|---|---|
| `0L` | single frame | L data bytes follow (1…7) |
| `1L LL` | first frame | 12-bit total length, then 6 data bytes |
| `2N` | consecutive frame | sequence number N = 1…F, 0, 1, …; 7 data bytes |
| `3S BS ST` | flow control | S: 0 continue, 1 wait, 2 overflow; block size; minimum separation time |

Reading the VIN shows all of them (`09 02` to 0x7DF):

```
7DF  02 09 02 55 55 55 55 55     tester: single frame, 2 bytes
7E8  10 14 49 02 01 57 53 49     ECM: first frame, 20 bytes in total
7E0  30 00 00 55 55 55 55 55     tester: flow control - send everything, no delay
7E8  21 4D 30 45 43 55 30 44     ECM: consecutive frame 1
7E8  22 49 41 47 32 30 32 36     ECM: consecutive frame 2
```

Unused bytes are padded (tester 0x55, ECUs 0xCC). Timeouts: 1 s for the flow control (N_Bs) and
between consecutive frames (N_Cr). The tester waits 150 ms for a response (P2).

## OBD-II (SAE J1979 / ISO 15031-5)

Answered by the ECM and the TCM. A positive response repeats the mode + 0x40.

| Mode | Request | Response | |
|---|---|---|---|
| 01 | `01 PID [PID…]` (up to 6) | `41 PID data …` | current data |
| 02 | `02 PID 00` | `42 PID 00 data` | freeze frame; PID 02 = the DTC that stored it |
| 03 | `03` | `43 count DTC…` | confirmed emission DTCs, 2 bytes each |
| 04 | `04` | `44` | clear all diagnostic information |
| 07 | `07` | `47 count DTC…` | pending DTCs |
| 09 | `09 type` | `49 type 01 data` | 02 VIN, 04 calibration ID, 0A ECU name |

| PID | Value | Formula |
|---|---|---|
| 00 / 20 / 40 | supported PIDs | bit mask, MSB = PID + 1 |
| 01 | monitor status | A bit 7 = MIL, A bits 0…6 = number of confirmed DTCs |
| 04 | engine load | A · 100 / 255 % |
| 05 | coolant temperature | A − 40 °C |
| 0C | engine speed | (256 A + B) / 4 rpm |
| 0D | vehicle speed | A km/h |
| 11 | throttle position | A · 100 / 255 % |
| 1F | run time since start | 256 A + B s |
| 42 | control module voltage | (256 A + B) / 1000 V |

Unsupported PIDs and modes are simply not answered.

## UDS (ISO 14229-1)

All four ECUs. A positive response is the service + 0x40; a negative one is `7F service NRC`.

| Service | Request | Notes |
|---|---|---|
| 0x10 DiagnosticSessionControl | `10 01` default, `10 03` extended | response carries P2 = 50 ms, P2* = 5000 ms |
| 0x11 ECUReset | `11 01` hard, `11 03` soft | the ECU is off the bus for 300 ms |
| 0x14 ClearDiagnosticInformation | `14 FF FF FF` all, or one DTC | |
| 0x19 ReadDTCInformation | `19 01 mask` count, `19 02 mask` list, `19 0A` all supported, `19 04 DTC 01` snapshot, `19 06 DTC 01` extended data | extended data = occurrence counter, aging counter |
| 0x22 ReadDataByIdentifier | `22 DID [DID…]` | |
| 0x27 SecurityAccess | `27 01` request seed, `27 02 key` | extended session only |
| 0x2F InputOutputControlByIdentifier | `2F DID 03 state` force, `2F DID 00` release | extended session and unlocked |
| 0x3E TesterPresent | `3E 00`, or `3E 80` without response | |

Bit 7 of a sub-function suppresses the positive response (`10 83`, `3E 80`).

**Sessions.** After power-up an ECU is in the default session. The extended session ends by itself
when no request arrives for 5 s (S3 timeout) — send TesterPresent to keep it. Leaving the session
locks the ECU again and releases all forced outputs.

**Security access.** Three wrong keys cause a 10 s lock-out. The algorithm is intentionally simple:

```
key = rotl32(seed XOR 0x5EC0DE5A, 5) + 0x1234ABCD
```

### Negative response codes

| NRC | Name | Typical cause here |
|---|---|---|
| 0x11 | serviceNotSupported | unknown service; OBD mode sent to ABS or BCM |
| 0x12 | subFunctionNotSupported | `10 02`, `19 55` |
| 0x13 | incorrectMessageLengthOrInvalidFormat | bytes missing or too many |
| 0x24 | requestSequenceError | key sent without requesting a seed |
| 0x31 | requestOutOfRange | unknown identifier or DTC |
| 0x33 | securityAccessDenied | IO control while locked |
| 0x35 | invalidKey | |
| 0x36 | exceededNumberOfAttempts | third wrong key |
| 0x37 | requiredTimeDelayNotExpired | seed requested during the lock-out |
| 0x7F | serviceNotSupportedInActiveSession | 0x27 / 0x2F in the default session |

With functional addressing an ECU does not send 0x11, 0x12, 0x31, 0x7E or 0x7F — otherwise every
ECU would reject a request meant for one of them.

### Data identifiers

| DID | ECU | Content | Encoding |
|---|---|---|---|
| F186 | all | active session | 1 byte |
| F187 / F18C / F195 | all | part number / serial number / software version | 12 ASCII |
| F190 | all | VIN | 17 ASCII |
| F4xx | ECM, TCM | OBD PID xx | as Mode 01 |
| F40D, F442 | ABS, BCM (F442) | vehicle speed, supply voltage | as PID 0D / 42 |
| 1001 / 1002 | ECM | coolant / throttle sensor voltage | 2 bytes, mV |
| 1003 | ECM | state: bit 0 running, bit 1 limp, bit 2 MIL | 1 byte |
| 2001 / 2002 / 2003 | TCM | gear / fluid temperature (−40) / sensor voltage mV | |
| 3001…3004 | ABS | wheel speed FL, FR, RL, RR | 2 bytes, 0.01 km/h |
| 3005 | ABS | brake pressure | 1 byte, bar |
| 4001 | BCM | battery voltage | 1 byte, 0.1 V |
| 4002 | BCM | lamps: bit 0 low, 1 high, 2 parking, 3 left, 4 right | 1 byte |
| 4003 | BCM | doors: bits 0…3 ajar FL/FR/RL/RR, bit 7 locked | 1 byte |

IO control identifiers: `3010` ABS warning lamp (ABS), `4010` low beam, `4011` door lock (BCM);
state `00` off, `01` on.

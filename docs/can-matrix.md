# CAN matrix

Classic CAN, 500 kbit/s, 11-bit identifiers, all signals little endian (Intel). The authoritative
definition is [`resources/dbc/vehicle.dbc`](../resources/dbc/vehicle.dbc); it opens in any DBC editor.

## Application messages

| ID | Name | Sender | Cycle | Receivers |
|---|---|---|---|---|
| 0x0B0 | ABS_WheelSpeeds | ABS | 10 ms | ECM |
| 0x0B1 | ABS_Status | ABS | 20 ms | ECM, TCM, BCM, IPC |
| 0x100 | ECM_Engine | ECM | 10 ms | TCM, ABS, IPC |
| 0x1A0 | TCM_Status | TCM | 20 ms | ECM, BCM, IPC |
| 0x300 | ECM_Status | ECM | 100 ms | TCM, BCM, IPC |
| 0x350 | BCM_Status | BCM | 100 ms | ECM, ABS, IPC |

Lower identifiers have higher priority: safety relevant chassis data comes first. With the ignition
on the bus carries 320 frames per second, about 8 % load.

### 0x0B0 ABS_WheelSpeeds

| Signal | Start bit | Length | Factor | Unit | Notes |
|---|---|---|---|---|---|
| WheelSpeed_FL / FR / RL / RR | 0 / 12 / 24 / 36 | 12 | 0.1 | km/h | 0 when the sensor delivers no pulses |
| WheelSpeed_xx_Valid | 48…51 | 1 | | | 0 = do not use this wheel |
| ABS_AliveCounter | 52 | 4 | | | +1 with every frame, wraps at 15 |
| ABS_Checksum | 56 | 8 | | | CRC-8 SAE J1850 over bytes 0…6 |

Counter and checksum form the end-to-end protection. A receiver that sees the same counter twice
knows the sender's application is stuck even though frames keep coming; a wrong CRC means the
content was corrupted. The ECM checks both and stores U0415.

### 0x0B1 ABS_Status

| Signal | Start bit | Length | Factor | Unit |
|---|---|---|---|---|
| VehicleSpeed | 0 | 16 | 0.01 | km/h |
| BrakePressure | 16 | 8 | 1 | bar |
| VehicleSpeedValid | 24 | 1 | | |
| AbsActive | 25 | 1 | | |
| AbsLampReq | 26 | 1 | | |
| TcsLampReq | 27 | 1 | | |

The vehicle speed is the mean of the valid wheels and stays valid with at least two of them.

### 0x100 ECM_Engine

| Signal | Start bit | Length | Factor | Offset | Unit |
|---|---|---|---|---|---|
| EngineRPM | 0 | 16 | 0.25 | 0 | rpm |
| ThrottlePos | 16 | 8 | 0.4 | 0 | % |
| PedalPos | 24 | 8 | 0.4 | 0 | % |
| EngineTorque | 32 | 12 | 0.5 | −500 | Nm |

### 0x300 ECM_Status

| Signal | Start bit | Length | Notes |
|---|---|---|---|
| CoolantTemp | 0 | 8 | offset −40 °C; the substitute value 80 °C while the sensor is faulty |
| MilReq | 8 | 1 | malfunction indicator lamp |
| CoolantWarnReq | 9 | 1 | above 112 °C |
| EngineRunning | 10 | 1 | |
| EngineLimp | 11 | 1 | throttle opening limited |

### 0x1A0 TCM_Status

| Signal | Start bit | Length | Notes |
|---|---|---|---|
| CurrentGear | 0 | 4 | 0 none, 1…6, 7 reverse |
| TargetGear | 4 | 4 | |
| GearSelector | 8 | 2 | 0 P, 1 R, 2 N, 3 D |
| ShiftInProgress | 10 | 1 | |
| LimpMode | 11 | 1 | fixed third gear |
| TcmMilReq | 12 | 1 | the TCM has no lamp of its own; the ECM stores P0700 and lights the MIL |
| TransTemp | 16 | 8 | offset −40 °C |

### 0x350 BCM_Status

| Signal | Start bit | Length | Notes |
|---|---|---|---|
| IgnitionState | 0 | 2 | 0 off, 1 on, 2 start |
| Headlamps | 2 | 2 | 0 off, 1 parking, 2 low beam |
| HighBeam, TurnLeft, TurnRight, DoorLock | 4…7 | 1 | turn signals already blink in the signal |
| DoorAjar_FL / FR / RL / RR | 8…11 | 1 | |
| LampFault | 12 | 1 | |
| BatteryVoltage | 16 | 8 | factor 0.1 V |

The BCM is on permanent supply and sends this message with the ignition off as well.

## Diagnostic identifiers (ISO 15765-4)

| ID | Meaning |
|---|---|
| 0x7DF | Functional request: every ECU listens, only single frames |
| 0x7E0 / 0x7E8 | ECM request / response |
| 0x7E1 / 0x7E9 | TCM request / response |
| 0x7E2 / 0x7EA | ABS request / response |
| 0x7E3 / 0x7EB | BCM request / response |

The response identifier is always the request identifier + 8. The instrument cluster has no
diagnostic server.

## Timeout supervision

| Receiver | Message | Timeout | DTC |
|---|---|---|---|
| ECM | ABS_Status 200 ms, TCM_Status 200 ms, BCM_Status 500 ms | | U0121, U0101, U0140 |
| TCM | ECM_Engine 200 ms, ABS_Status 200 ms | | U0100, U0121 |
| ABS | ECM_Engine 200 ms, BCM_Status 500 ms | | U0100, U0140 |
| BCM | ECM_Status 1000 ms, TCM_Status 500 ms, ABS_Status 500 ms | | U0100, U0101, U0121 |
| IPC | all of the above | | no DTC; lights the lamp of the silent ECU |

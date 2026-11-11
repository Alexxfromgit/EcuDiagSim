# Architecture

## Two layers

- **`ecusim_core`** (`src/core`) is a static library without any UI code. It depends on QtCore and
  QtSerialBus only and contains the complete vehicle. Everything in it is unit-testable.
- **`EcuDiagSim`** (`src/app`) is the Qt Quick front end: three C++ view models and QML.

## The simulation loop

`Simulation::step()` advances the world by exactly 1 ms. The core never reads the wall clock, so a
run is reproducible bit for bit — the test `tst_vehicle::deterministic` checks that. The UI calls
`step()` from a timer as often as the chosen speed factor requires; tests call it in a tight loop
and run minutes of driving in well under a second.

One step, in order:

1. `VehicleModel::step` — the plant: engine, driveline, temperatures, battery.
2. `SensorSet::update` — every sensor samples the plant and applies its injected fault.
3. `Ecu::tick` for ECM, TCM, ABS, BCM, IPC — each runs its cyclic tasks that are due.
4. `DiagnosticTester::tick` — scan tool timing (P2, tester present).
5. `VirtualCanBus::tick` — arbitration; frames are delivered to the other nodes.
6. `ScenarioRunner::tick` — the running lesson, if any.

```
Simulation
 ├─ VehicleModel        plant; ECUs act on it through throttleCmd / fuelEnable / gearCmd
 ├─ DriverInputs        ignition, pedals, selector, lights, doors
 ├─ SensorSet           Sensor = transfer curve + injected Fault
 ├─ VirtualCanBus       pending queue, lowest ID wins, bit-time accounting
 ├─ Ecm Tcm AbsEcu Bcm Ipc   (all derive from Ecu, which is a CanNode)
 ├─ DiagnosticTester    CanNode; ISO-TP client
 ├─ TraceRecorder / TraceReplayer
 └─ ScenarioRunner
```

## Plant and controllers

The vehicle model knows nothing about ECUs. Controllers read it *only through sensors* and act
through three actuator values. That separation is what makes faults behave realistically: when the
crankshaft sensor is unplugged the engine is fine, but the ECM sees no speed signal, stops injecting
and the plant stalls.

Analog sensors are modelled as a voltage on a 5 V input. A coolant thermistor works against a
pull-up, so an open circuit reads 5 V (−40 °C) and a short to ground 0 V (150 °C); the ECM tells the
two apart by the voltage, exactly why P0117 and P0118 are different codes.

## The bus

`VirtualCanBus` queues frames per tick and lets the lowest identifier win. Each frame occupies the
bus for its bit time (47 + 8·DLC bits plus stuffing at 500 kbit/s), which gives time stamps with
sub-millisecond spacing and a meaningful bus load figure. A node that is unpowered or bus-off
neither sends nor receives.

Signals are never packed by hand: ECUs call `send(id, {{"EngineRPM", rpm}, …})` and
`DbcDatabase` — a thin wrapper around `QCanDbcFileParser` and `QCanFrameProcessor` — encodes them
according to `vehicle.dbc`. The trace view decodes with the same database.

## An ECU

`Ecu` provides what all control units share:

- **Power**: terminal 15 (ignition) or terminal 30 (BCM, always on); injected power loss; reset.
- **Scheduler**: `every(periodMs, task)`.
- **Receive supervision**: `subscribe(id, timeoutMs, dtc)` stores decoded signals and reports a
  *lost communication* DTC when a message stays away. Supervision starts one second after wake-up.
- **Fault memory** (`DtcManager`) and **diagnostic server** (`DiagnosticServer`) behind an ISO-TP link.

The derived classes add sensors, monitors and control logic. A monitor is one line:
`dtcs().report(P0118, volts > 4.8)`. Debouncing, status bits, trips, snapshots and lamp requests
happen in `DtcManager`.

## Fault memory

`DtcManager` follows ISO 14229-1 Annex D / AUTOSAR Dem:

- Counter debounce: +1 per failed result, −1 per passed, with a jump to zero when the direction changes.
- `testFailed` sets `pending`; `confirmed` follows after `tripsToConfirm` operation cycles with a failure.
- An operation cycle is an ignition cycle. A complete cycle without a failure clears `pending`,
  counts towards healing (warning indicator off) and aging (confirmed off after 40 cycles).
- The snapshot is taken once, when the fault first qualifies.

## Diagnostics

```
DiagnosticTester ── IsoTpLink ── CAN ── IsoTpLink ── DiagnosticServer ── DtcManager
```

`IsoTpLink` implements single / first / consecutive / flow control frames with block size, STmin and
the N_Bs / N_Cr timeouts; both ends use the same class. `DiagnosticServer` works on whole messages:
`handleRequest(bytes, functional)` returns the response or nothing. OBD PIDs are registered once and
are readable three ways: Mode 01, Mode 02 from the stored snapshot, and as UDS identifier
`0xF400 + PID`.

## Application

`AppController` is the QML singleton `App`. It owns the `Simulation`, steps it in real time and
exposes driver inputs, cluster state, faults and lessons. `TraceModel` batches frames into a list
model at 30 Hz. `ScannerViewModel` turns button presses into requests and responses into tables.

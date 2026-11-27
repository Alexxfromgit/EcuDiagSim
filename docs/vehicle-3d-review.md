# Independent review and regression verification

Reviewed on 2026-11-27. A separate reviewer session, given the repository and commit range
without the implementation conversation, examined `c4e46cc..bdb4c62`. It reviewed the native
integration, original 2D workflow, state/animation semantics, dependency resources, packaging
rules and tests, then independently re-reviewed the fixes below.

## Finding and fix

**P2: broken component circuits could continue animating.** The viewer detected sensor opens
and shorts from sensor metadata, but airbag squibs, belt switch and low-beam bulb are Component
targets without a sensor object. Their broken paths therefore remained active.

The app now publishes read-only physical `vehicle.componentCircuits` values for those components.
Squib resistance uses the same thresholds as the SRS module and accounts for deployed airbags.
The viewer uses those values for flow, marker state and inspector circuit text. Opens/shorts
stop flow; repairs restore it. A missing adaptation does not invent a broken electrical wire.
Native fault/repair snapshots, dependency-free JS tests and the browser suite cover this.
The independent reviewer cleared the finding and reported no further actionable issues.

## Original behavior comparison

The same `legacyBehaviorSnapshots` test was compiled against both the last 2D-only revision,
`c4e46cc`, and the reviewed implementation, using Qt 6.12.0, AppleClang 17 and Debug builds.
All **29 named checkpoints matched exactly**, including:

- Every original vehicle field, cluster state, sensor values, ECU state, fitted targets and faults.
- OBD/UDS results, scanner log/session state, and SHA-256 digests over all CAN trace row values
  (including payloads, timestamps, sender, counts and cycle times).
- All 16 equipment combinations; paused startup; lighting/hazards; doors/locks/restraints;
  steering, brake, reverse and road surface; engine startup/driving; injected faults;
  actuator diagnostics; filtered/fixed/paused trace; repair and reset.

Only additive 3D display metadata is excluded from that comparison; every original field,
including the original `locked` result, is retained. The test also repeatedly reads display
state and checks that neither the original outputs nor the complete vehicle snapshot changes.
Core simulation, ECU, protocol and diagnostic implementations remain unchanged. Original 2D
QML behavior remains unchanged; test object names were added for native UI verification.

To repeat the comparison, export `legacyBehaviorSnapshots` from the current test executable.
Extract `c4e46cc` into an ignored build directory, copy the current `tests/tst_app.cpp` into
that checkout, configure it with `ECUSIM_BUILD_APP=OFF`, `ECUSIM_UPDATES=OFF` and the same Qt/
compiler/build type, and build/run only `tst_app legacyBehaviorSnapshots` with a different
`ECUSIM_LEGACY_SNAPSHOTS` output path. Parse and compare the two JSON documents. The baseline
checkout's application and core sources must remain untouched.

## Validation

- Warnings-as-errors builds with `ECUSIM_VEHICLE_3D=ON` and `OFF`.
- **11/11 CTest suites passed in each build**, including the original simulator/diagnostic tests,
  the display-read regression test, native QML workspace tests, and 11 JS test cases.
- Isolated Chrome/WebGL checks passed all **16 equipment combinations and 32 native control/
  fault states**, including component opens/shorts/repairs and deployed squibs, lamp banks,
  blink phases, power loss, steering, doors, fuel, restraints and wheel motion.
- The independent reviewer reran the viewer, AppController and native QML tests and independently
  checked the zero-difference baseline comparison.
- A native Qt WebEngine smoke check passed physical component circuit state/blocked flow through
  WebChannel, live sensor values, both hazard phases, lamp/door outputs, and fault-editor/scanner
  navigation while retaining the viewer across expansion and collapse.

These results establish local macOS behavior for the tested scenarios. The QML workspace test
uses the real 2D diagram and a fake 3D component for lifecycle checks; it does not replace native
WebEngine testing. Windows/Linux packaged graphics and release dependency/source-distribution
obligations still need their platform release checks. No proven packaging regression was found.

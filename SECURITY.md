# Security policy

The simulator runs entirely on your computer and talks to no network or hardware, but it does read
files that may come from other people: scenario JSON files, CAN logs for replay and the DBC database.

## Reporting a vulnerability

Please **do not open a public issue**. Report it privately through
[GitHub's private vulnerability reporting](https://github.com/Alexxfromgit/EcuDiagSim/security/advisories/new)
(Security tab → *Report a vulnerability*). Include the version, OS, steps to reproduce and the
impact you see.

You can expect an acknowledgement within a few days. Once a fix is released, the advisory is
published with credit to the reporter unless you prefer otherwise.

## Supported versions

Only the latest release receives fixes.

## Scope

Of particular interest:

- Crashes or memory corruption from malformed input files: scenarios (`src/core/sim/ScenarioRunner.cpp`),
  candump / Vector ASC logs (`src/core/trace/TraceRecorder.cpp`), DBC files.
- Anything that makes the application write outside the file a user chose when saving a trace.

The security access algorithm of the simulated ECUs (UDS service 0x27) is deliberately trivial and
documented; it is not a vulnerability.

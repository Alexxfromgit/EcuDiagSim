# Changelog

## [0.4.0](https://github.com/Alexxfromgit/EcuDiagSim/compare/v0.3.1...v0.4.0) (2026-11-28)


### Features

* add offline 3D vehicle explorer with live controls and wiring ([#14](https://github.com/Alexxfromgit/EcuDiagSim/issues/14)) ([9a44c3f](https://github.com/Alexxfromgit/EcuDiagSim/commit/9a44c3f621fccfd5558558233d057fc7b07fa302))

## [0.3.1](https://github.com/Alexxfromgit/EcuDiagSim/compare/v0.3.0...v0.3.1) (2026-11-26)


### Bug Fixes

* keep tooltips off the element they describe so clicks reach it ([#12](https://github.com/Alexxfromgit/EcuDiagSim/issues/12)) ([26cc92a](https://github.com/Alexxfromgit/EcuDiagSim/commit/26cc92abea808b4976f33e90a14f4b974836dac9))

## [0.3.0](https://github.com/Alexxfromgit/EcuDiagSim/compare/v0.2.0...v0.3.0) (2026-11-26)


### Features

* airbag system with SRS module, crash record and belt reminder, three lessons ([#10](https://github.com/Alexxfromgit/EcuDiagSim/issues/10)) ([95040f7](https://github.com/Alexxfromgit/EcuDiagSim/commit/95040f7c2f3d7c0b6dc491a7d719b4495cf98f6f))
* coding (UDS 0x2E) and routines (UDS 0x31), replaced-part faults, three lessons ([#6](https://github.com/Alexxfromgit/EcuDiagSim/issues/6)) ([bac8e17](https://github.com/Alexxfromgit/EcuDiagSim/commit/bac8e17cf7d2d47ea8a314a2dfdccf78d993132d))
* engine OBD pack with closed-loop fuelling, fuel trims and misfire detection ([#8](https://github.com/Alexxfromgit/EcuDiagSim/issues/8)) ([a0c4f9e](https://github.com/Alexxfromgit/EcuDiagSim/commit/a0c4f9ed2509c82386f311350405b562c6da61b8))
* nine new lessons, a redrawn car with working lights, adaptive layout ([#3](https://github.com/Alexxfromgit/EcuDiagSim/issues/3)) ([ba3de74](https://github.com/Alexxfromgit/EcuDiagSim/commit/ba3de74da3615b2250c863e6f6c2e49a608c8636))
* open paused, settings page, equipment plumbing and smoother animations ([#5](https://github.com/Alexxfromgit/EcuDiagSim/issues/5)) ([1fcc05c](https://github.com/Alexxfromgit/EcuDiagSim/commit/1fcc05c697ac8f3b25698f7f012a6eec3b1aff57))
* park brake, fuel system with consumption, oil pressure, four lessons ([#7](https://github.com/Alexxfromgit/EcuDiagSim/issues/7)) ([d439e06](https://github.com/Alexxfromgit/EcuDiagSim/commit/d439e0652e046f1531bb30afeef527defbe53026))
* steering, road surfaces, real ABS slip control and stability control (ESC) ([#11](https://github.com/Alexxfromgit/EcuDiagSim/issues/11)) ([d684897](https://github.com/Alexxfromgit/EcuDiagSim/commit/d68489738a9365f296a7a03503821fa8271cd8d0))
* tyre pressure monitoring with relearn routine, FMVSS lamp and three lessons ([#9](https://github.com/Alexxfromgit/EcuDiagSim/issues/9)) ([41df06a](https://github.com/Alexxfromgit/EcuDiagSim/commit/41df06a3da5d9a8f8ac165092f4fe57f4661747b))

## [0.2.0](https://github.com/Alexxfromgit/EcuDiagSim/compare/v0.1.0...v0.2.0) (2026-11-11)


### Features

* installers and automatic updates for Windows, macOS and Linux ([#1](https://github.com/Alexxfromgit/EcuDiagSim/issues/1)) ([ceeb0b3](https://github.com/Alexxfromgit/EcuDiagSim/commit/ceeb0b32df6f379ecbb8ad35d10b304b2d981ffe))

## 0.1.0


### Features

* virtual vehicle with engine, transmission, ABS, body control module and instrument cluster on a simulated 500 kbit/s CAN bus, defined by a DBC file
* fault injection into sensors, components, control units and the bus: open circuit, shorts, stuck, offset, gain, noise, intermittent and erratic signals, power loss, bus-off, end-to-end protection errors
* fault memory with ISO 14229 status bits, debouncing, pending and confirmed DTCs, warning lamps, healing, aging and snapshots
* scan tool with OBD-II modes 01, 02, 03, 04, 07 and 09 and UDS services 0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2F and 0x3E over ISO-TP
* decoded CAN trace with candump and Vector ASC export and log replay
* four guided lessons (wheel speed sensor, coolant sensor, low battery, ABS module offline) and a JSON format for your own

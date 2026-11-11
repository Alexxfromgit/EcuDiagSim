# Changelog

## 0.1.0


### Features

* virtual vehicle with engine, transmission, ABS, body control module and instrument cluster on a simulated 500 kbit/s CAN bus, defined by a DBC file
* fault injection into sensors, components, control units and the bus: open circuit, shorts, stuck, offset, gain, noise, intermittent and erratic signals, power loss, bus-off, end-to-end protection errors
* fault memory with ISO 14229 status bits, debouncing, pending and confirmed DTCs, warning lamps, healing, aging and snapshots
* scan tool with OBD-II modes 01, 02, 03, 04, 07 and 09 and UDS services 0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2F and 0x3E over ISO-TP
* decoded CAN trace with candump and Vector ASC export and log replay
* four guided lessons (wheel speed sensor, coolant sensor, low battery, ABS module offline) and a JSON format for your own

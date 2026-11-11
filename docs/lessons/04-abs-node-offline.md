# Lesson 4 — ABS control module offline

**Goal:** understand network (U) codes, timeout supervision and knock-on effects between ECUs.

## Background

A dead control unit cannot store a DTC about its own death. The others notice instead: every
receiver knows which messages it expects and how often. When one stays away longer than its timeout,
the receiver stores **U01xx — lost communication with …**.

## Steps

1. Ignition on, start the engine, selector **D**, drive at about 30 km/h.
2. In the car, right-click the **ABS** box (or pick *ABS – Anti-lock brake system* in the Faults
   tab) → *Power supply lost* → **Inject fault**.

## What to observe

- Trace → **By ID**: `0B0` and `0B1` stop updating. The bus load drops.
- Cluster: **ABS** and **TCS** light although nobody requested them — the cluster's fail-safe for a
  silent ABS. The speedometer falls to 0.
- Car view: ABS shows *NO POWER*; ECM, TCM and BCM each gain a DTC.
- A second later **CHECK** lights and the TCM gains another code.

| ECU | DTC | Why |
|---|---|---|
| ECM, TCM, BCM | U0121-87 | `ABS_Status` missing |
| TCM | P0500 | no vehicle speed; it now estimates the speed from engine speed and gear |
| ECM | P0700 | the TCM requested the MIL with `TcmMilReq` |

Read from the bottom up this is a **causal chain**: no ABS → no speed for the TCM → TCM asks for
the MIL → ECM lights it. A technician who starts at P0700 or P0500 will be looking in the wrong
place; the U-codes in three modules point straight at the ABS.

## With the scan tool

- UDS → **ABS** → any request: *No response*. A module that does not answer is itself a finding.
- UDS → **ECM** → **Read DTCs**: `C1 21 87` = U0121-87.
- OBD-II → **Read codes**: P0700 from the ECM and P0500 from the TCM. The U-codes are not emission
  relevant and do not appear here.

## Repair

Repair the ABS. It boots, the messages return, the ABS lamp goes out. Ask the ABS for its DTCs:
nothing — it was simply absent. The other modules keep their entries until cleared.

## Try next

- *CAN bus-off* on the TCM instead of power loss: the TCM is alive and stores U-codes about
  everybody else, while everybody else stores U0101.
- *CAN wiring short*: every module loses every other one. Note that the scan tool is cut off too.
- *ABS wheel speed message protection → Wrong checksum*: the frames arrive on time, so there is no
  timeout, but the ECM rejects the content (U0415-83).
- Power the **ECM** down while driving and watch the transmission go into limp mode.

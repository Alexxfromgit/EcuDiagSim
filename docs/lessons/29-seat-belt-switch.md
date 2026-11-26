# Lesson 29 — Seat belt reminder and an open buckle switch

*Needs the **Airbag system (SRS)**; the lesson fits it for its run.*

**Goal:** see why safety switches are resistor coded, and how a module behaves when it cannot
trust an input.

## A switch with three states

A plain switch is either open or closed. If its wire breaks, "open" is indistinguishable from a real
"off". The buckle switch therefore has two resistors: one value for *buckled*, another for
*unbuckled*. An **open circuit** is a third, invalid state the airbag module recognises — **B0050-13**.

## Steps

1. Ignition on, start the engine, belt unbuckled: **BELT** is on; above 20 km/h it flashes
   (`BeltChime`).
2. Buckle up (Driver controls → *Seat belt*): the reminder goes out.
3. Click **BELT** under the driver's seat → *Open circuit*.

## What to observe

| Where | What |
|---|---|
| SRS | **B0050-13** after 2 s |
| DID `5003` | `02` — bit 1, circuit open (it read `01` = buckled before) |
| Cluster | **BELT** on although buckled — and **AIRBAG**, because the belt status changes how the airbags fire |

When an input cannot be trusted, a safety system assumes the worse case: here, an unbelted driver.

## Try next

- Lesson 20 (park brake): a plain switch that sticks closed — no DTC at all. Compare the two designs.

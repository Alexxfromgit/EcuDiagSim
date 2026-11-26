# Lesson 27 — Airbag lamp: open driver squib circuit

*Needs the **Airbag system (SRS)** (Settings → Vehicle equipment); the lesson fits it for its run.*

**Goal:** understand how an airbag module monitors itself, why its lamp has its own self test, and
why a code that "comes back" after clearing is not a scan tool problem.

## How the module watches the squibs

An airbag is fired by a squib — a small igniter with about 2 Ω. The module sends a tiny test current
through each squib circuit all the time (far too small to fire it) and expects 1–4 Ω:

- below 1 Ω: the leads touch → **-1A** *resistance below threshold*;
- above 4 Ω: the circuit is open → **-1B** *resistance above threshold*.

The driver's squib sits in the steering wheel. Its wires pass a **clock spring**: a flat ribbon cable
wound up in the steering column that lets the wheel turn. Worn clock springs are the classic cause of
an airbag lamp.

## Steps

1. Ignition on. The airbag lamp is on for 6 s — the module's own lamp check, separate from the
   cluster's 3 s bulb check — then goes out.
2. Click **AB** at the steering wheel → *Open circuit* → **Inject fault**.

## What to observe

| Where | What |
|---|---|
| SRS (scan tool target `SRS 0x7E4`) | **B0001-1B** after 2 s |
| DID `5001` | `FF FF` — open (it read `00 DC` = 2.20 Ω before) |
| Cluster | **AIRBAG** on |
| Clear DTCs · 14 | accepted (`54`), but the code is back 2 s later: the test runs continuously |

## Try next

- *Short to ground* on the passenger squib: **B0010-1A**.
- Code the passenger airbag out (DID `0100` = `02`, extended session + unlock): the module stops
  testing the passenger circuit — exactly what happens with a wrongly coded replacement module.

# Lesson 23 — Dirty air flow meter: P0101 and a substitute model

*Needs the **Engine OBD pack**; the lesson fits it for its run.*

**Goal:** tell a sensor that lies from air that bypasses it, and see how an ECM switches to a model
when it stops trusting a sensor.

The ECM has two ways to know the air mass:

- the **air flow meter** (MAF), a heated film in the intake;
- a **speed-density model**: engine speed × throttle opening × displacement × filling.

Normally they agree. P0101 (*range/performance*) sets when they differ by more than 30 % for 5 s
with a warm engine.

## Steps

1. Ignition on, start, wait at warm idle.
2. Faults → *Sensors* → **Mass air flow sensor** → *Gain error*, factor `0.6` → **Inject fault**
   (or **MAF** in the car).

## What to observe

| Time | What |
|---|---|
| first seconds | the meter reads 60 % of the real air, the ECM injects too little: lean, STFT climbs |
| after 5 s | **P0101** pending: the meter disagrees with the model |
| then | the ECM fuels from the model (substitute value): lambda back at 1, the trims come back |
| car view sensor table | real ≈ 2.2 g/s, ECU measures ≈ 1.3 g/s |

## Vacuum leak or dirty meter?

Both make the engine lean and push the trims up. The difference:

| | vacuum leak (lesson 21) | dirty meter |
|---|---|---|
| meter versus model | agree | disagree → P0101 |
| trims after a while | stay high (P0171) | come back once the ECM uses the model |
| worst at | idle | wherever the error is largest |

## Try next

- Unplug the meter (*Open circuit*): **P0102** within a second, the model takes over at once and the
  engine never runs lean.
- After the repair, reset the trims (routine `0x0202`): what was learned while the meter lied is wrong.

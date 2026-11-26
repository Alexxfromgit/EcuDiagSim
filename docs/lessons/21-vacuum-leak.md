# Lesson 21 — Vacuum leak: fuel trims and P0171

*Needs the **Engine OBD pack** (Settings → Vehicle equipment); the lesson fits it for its run.*

**Goal:** read fuel trims like a technician: what the short-term and long-term values say, why a
lean code is a symptom and not a part, and why adaptations have to be reset after a repair.

## How the mixture control works

The ECM meters fuel from the air flow meter: air ÷ 14.7 = fuel for lambda 1. The upstream oxygen
sensor (B1S1) only knows *rich* (≈ 0.8 V) or *lean* (≈ 0.1 V). In closed loop the ECM keeps
switching it: a jump and a slow ramp in the **short-term fuel trim** (STFT) every time the signal
crosses 0.45 V. Watch it in the trace (`320 ECM_Mixture`): the O2 voltage swings 0.2…0.7 V a few
times per second.

A correction that persists moves into the **long-term fuel trim** (LTFT), stored in the ECM. A new
car already learns a few percent: the injectors flow slightly less than nominal.

## Steps

1. Run the lesson, or by hand: ignition on, start, wait 10 s at idle.
2. Faults → *Components* → **Intake hose (vacuum leak)** → *Component failure* (or **VAC** in the car).

## What to observe

| Where | What |
|---|---|
| `320 ECM_Mixture` | O2 voltage stays low, STFT climbs, then LTFT takes over |
| OBD live data | PID 06 / 07 short / long term fuel trim; PID 14 O2 voltage |
| ECM | **P0171** system too lean — pending after one trip (Mode 07), confirmed in the second, then the MIL |
| ECM | no P0101: the air flow meter correctly measures what passes it; the extra air enters behind it |

A leak hurts most at idle, where it is a large share of the air: drive with 40 % throttle and the
short-term trim goes back towards zero while the long-term value stays high.

## After the repair

The new hose stops the leak, but the ECM still adds the learned 20–25 %: now the engine runs rich
and the short-term trim goes negative. Scan tool → UDS → **ECM** → **Extended session** →
*Reset fuel trims* **Start** (routine `0x0202`). The trims relearn the small injector tolerance only.
Then clear the codes.

## Try next

- Inject a misfire (lesson 22) and watch the trims: unburned oxygen makes the sensor read lean, the
  ECM adds fuel — a misfire can cause a lean code as a side effect.

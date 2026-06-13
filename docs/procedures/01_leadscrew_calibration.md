# Procedure 01 — Leadscrew mm-per-pulse Calibration

**Goal:** measure how far the actuator travels per STEP pulse, so every later
sweep reports true millimetres. This replaces the inherited `0.0001` constant
with a value we measure and document ourselves.

**Sketch:** `firmware/01_leadscrew_calibration/01_leadscrew_calibration.ino`
**Sensor needed:** no — this step is motion only.

---

## Background (why 2000 pulses = 1 revolution)

The Geckodrive G251 is a **fixed 10-microstep driver**: every STEP pulse is
1/10 of a full step. With a 200 full-step motor:

```
pulses per revolution = 200 full steps × 10 microsteps = 2000
```

The leadscrew *lead* (mm travelled per revolution) is a mechanical property of
the screw and is **not** in any datasheet, so we measure it.

---

## Pre-checks

- [ ] STEP → ESP32 GPIO18 → G251 Terminal 10
- [ ] DIR → ESP32 GPIO19 → G251 Terminal 9
- [ ] ESP32 GND ↔ G251 Terminal 12 (common logic ground) — **must** be shared
- [ ] G251 powered (15–50 VDC), motor phases A /A B /B wired, I-set resistor in place
- [ ] Carriage is free to move and you know its mechanical travel limits

## Setup

1. Arduino IDE → Board: **ESP32 Wrover Module**, Upload Speed 115200.
2. Upload the sketch.
3. Open Serial Monitor: **115200 baud**, line ending **Newline**.
   You should see the menu banner.

---

## Step A — Confirm direction

1. Type `100` and press enter. The carriage moves a small amount.
2. Watch which way it goes.
   - Moves **away** from the sensor → convention is correct (positive = away).
   - Moves **toward** the sensor → open the sketch, change
     `DIR_AWAY_LEVEL` from `HIGH` to `LOW`, re-upload, and re-test.
3. Record the result below.

## Step B — Measure travel

1. Move the carriage to a repeatable **datum** (a hard stop, or align a marked
   edge of the carriage to a ruler/caliper reference). Small jogs like `200`
   or `-200` help you nudge it there.
2. Type `zero`. The counter is now your datum.
3. Type `20000` (= 10 revolutions). Wait for `[done]`.
4. With a caliper or ruler, measure the **actual travel** from the datum.
   Record it in mm.
5. Type `mm <value>` (e.g. `mm 8.04`). The sketch prints mm/pulse,
   pulses/mm, and mm/rev.

## Step C — Repeat and average

Do Step B **at least 3 times**. Then repeat it moving in the **opposite
direction** (jog negative) — any difference between the away and toward results
is **backlash**, which we want to know about now.

---

## Results — record here

| Run | Direction | Pulses | Travel (mm) | mm/pulse | mm/rev |
|-----|-----------|--------|-------------|----------|--------|
| 1   | away      | 20000  |             |          |        |
| 2   | away      | 20000  |             |          |        |
| 3   | away      | 20000  |             |          |        |
| 4   | toward    | 20000  |             |          |        |
| 5   | toward    | 20000  |             |          |        |

**Adopted mm/pulse (average):** ________________
**Backlash observed (away vs toward):** ________________
**Date / operator / rig notes:** ________________

---

## Notes & gotchas

- **Missed steps corrupt the result.** If the motor sounds rough, buzzes, or
  stalls, it is dropping pulses. Increase `PULSE_HIGH_US` / `PULSE_LOW_US` in
  the sketch (slower = more reliable) and redo the run.
- **Stay inside the travel limits.** The sketch refuses any single move above
  `MAX_PULSES_PER_MOVE` (default 40000 = 20 rev) as a guard; jog in stages.
- Use the **longest** travel your screw allows for each run — more pulses per
  measurement means caliper reading error matters less.
- Once `mm/pulse` is adopted, it becomes a constant in the next sketch
  (the characterization sweep). We never hand-tune it again.

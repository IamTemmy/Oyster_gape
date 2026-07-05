# Procedure 03 — Per-Range Linearization

**Goal:** for each magnetic range, straighten the sensor's output so it reads
proportional to gap, using the HAL 2425's 16 setpoints, and verify it.

**Sketch:** `firmware/03_linearization/03_linearization.ino` (modes: `cal`, `lin`, `verify`)
**Logger:** `host/sweep_logger.py` (captures the `verify` sweep)

This is the same procedure for all eight ranges — only the **window** changes.

---

## Windows (START / END per range) — verified

These are the **corrected, verified** windows — the value you type: `window START END`.
Each START was pushed past that range's steep shoulder; the initial guesses were too
low and made the linearizer's inverse spike (F:000000, unwritable). Full narrative in
`data/analysis/linearization_log.md`.

| Range | START (mm) | END (mm) | Status |
|-------|-----------|----------|--------|
| 100 mT | 4.0 | 14.0 | verified (2.6%) |
| 50 mT | 6.0 | 16.0 | verified (1.4%, cleanest) |
| 25 mT | 8.0 | 20.0 | verified (2.0%) — bench build |
| 12 mT | 11.0 | 24.0 | verified (2.3%) |
| 6 mT | 17.0 | 32.0 | verified (2.6%) |
| 3 mT | 20.0 | 38.0 | verified (3.1%) |
| 200 mT | — | — | **IMPRACTICAL** — swing vs gentle-start squeeze |
| 400 mT | — | — | **IMPRACTICAL** — swing too small to calibrate |

START sits just past each range's steep shoulder (further out as sensitivity rises —
a clipped/flat or too-steep span can't be linearized); END sits before the flat tail.
The table gives the **window command**; the *measured* ramp runs a little wider (e.g.
25 mT command 8.0–20.0 → measured ramp ~8.5–20.7, see the range ladder in
`linearization_log.md`). **200 mT and 400 mT do not linearize with this magnet**, so
start the campaign at **100 mT** and work down the table.

## Settings (same for every range)

- **OUT_MIN / OUT_MAX = 10% / 90%** — keeps the signal inside the sensor's
  full-accuracy band (~7–93% VSUP) and leaves headroom for wire-break/clamp
  diagnostics. Don't use 0/100.
- **SCALE_MIN / SCALE_MAX = 1024 / 29696** (the tool defaults) — uses most of
  the internal 0–32767 range.

## Connections — which device owns the OUT line

- **cal / lin:** the **Micronas programmer reads the sensor** → programmer
  connected. (The ESP32 only drives the motor here; it does not read OUT.)
- **verify:** the **ESP32 reads the analog output** → **disconnect the
  programmer** from the OUT line first (the "lines flying" problem).
- STEP/DIR (motor) stay connected the whole time.
- If Micronas comms glitch during cal/lin, also lift the ÷1.5 divider off OUT so
  only the programmer is on it.

---

## Per-range loop

1. **Reset + range** (Sensor Settings): Set Default Values → Magnetic Range =
   target → Write Customer Setup → Power On Reset. (Clears any prior
   linearization so ranges don't stack.)
2. **Set the window in the sketch:** rest the carriage gently on the block →
   `zero` → `window <START> <END>` for this range.
3. **2-Point Calibration** (Micronas tool): enter SCALE_MIN/MAX defaults and
   OUT_MIN/MAX = 10/90 → in the sketch run **`cal`**:
   - it goes to START → click **Calibration Point 1** → press ENTER
   - it goes to END → click **Calibration Point 2** → press ENTER
   - then **Calculate Signal Path** → **Write Signal Path**.
4. **16-point measurement** (Measurement Tool open): in the sketch run **`lin`**:
   - at each of the 16 stops, register the reading in the tool, press ENTER
   - when done, the tool **saves the setpoint file** itself.
5. **Write setpoints** (Linearization Tool): load that file → **Write Setpoints**.
6. **Verify:** disconnect the programmer from OUT. In the logger set
   `:range <range>mT_linearized`, then in the sketch run **`verify`**. The logger
   saves `charac_<range>mT_linearized_run1.csv` + `.png`.

**A good verify looks like:** flat clamp (10%) → straight diagonal ramp across
the window → flat clamp (90%). The straight ramp is the proof. Overlay it on the
raw `charac_<range>mT` curve for the before/after figure.

---

## Sketch command reference

```
zero              set datum at current position (do this on the block)
window <a> <b>    set window, e.g.  window 1.0 10.0
cal               visit START then END (ENTER to advance, q to abort)
lin               walk 16 even stops START->END (ENTER each, q to abort)
verify            100 mm sweep, streamed to the logger
<integer>         jog pulses (1000 = 1 mm, + = away from sensor)
pos / help
```

One command at a time; wait for each reply (avoids the merged-line problem).

## Records to keep per range

Sensor ID/channel, range, window START/END, OUT_MIN/MAX, the saved setpoint
file, and the `verify` CSV/PNG. These are the per-unit calibration identity.

## Notes

- Output falls with gap (our polarity), so the linearized ramp will also be a
  clean monotonic line — direction doesn't matter to the linearizer.
- `cal`, `lin`, `verify` all return to the datum (block) when finished, so you
  don't reposition between steps. Re-`zero` on the block if you suspect drift.

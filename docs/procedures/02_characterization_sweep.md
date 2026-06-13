# Procedure 02 — Characterization Sweep & Range Selection

**Goal:** capture the magnet's response curve (distance vs sensor output) and use
it to choose the magnetic range — **25 mT vs 50 mT** — for this magnet and the
~10 mm working window.

**Sketch:** `firmware/02_characterization_sweep/02_characterization_sweep.ino`
**Uses:** the Stage 01 constant, `0.001 mm/pulse`.

---

## Why this step decides the range

For the current magnet, the prior team's **±3 mT is blind below ~12 mm**, so in a
0–10 mm window it sees nothing. The candidates are:

- **±50 mT** — expected to climb cleanly from ~0 mm (full window usable, lower slope).
- **±25 mT** — more sensitive, but may clip in roughly the first ~2 mm.

We sweep at each and pick the **smallest range (highest sensitivity) whose curve
climbs cleanly across the whole window without a flat/clamped near end.**

---

## Order of operations (read this first)

This stage captures the **raw, un-linearized** sensor output. The sensor and
magnet must be physically mounted, and the magnetic range programmed, *before*
any sweep — the curve depends entirely on the geometry and range.

1. **Mount** the sensor in its fixed measurement position (sensitive face toward
   the magnet's travel axis); mount the magnet on the carriage, aligned, within
   the ~10 mm travel. **Freeze this geometry** — re-mounting later invalidates
   the characterization and any setpoints derived from it.
2. **Program the range** (Micronas app + programmer): Sensor Settings → Customer
   Setup → Magnetic Range → Write Customer Setup → Power On Reset. Leave
   linearization at **default (raw)**.
3. **Run standalone:** power the sensor from the board's 5 V, output → ÷2 divider
   → GPIO34. The Micronas app does **not** need to run during the sweep. If the
   programmer shares the OUT line, disconnect/power it down for the measurement
   so it doesn't load the signal.
4. **Sweep** with the ESP32 sketch; save the raw curve.
5. Repeat 2–4 for the other range, then choose.

The Micronas app returns in Stage 03 to ingest these raw curves and program the
setpoints. Characterization (here) is raw; linearization is later.

> Tip: the first time the sensor chain is live on a fresh rig, do one throwaway
> sweep at ±50 mT as a pure rig check (sensible climbing curve, sane noise, clean
> return) before the formal 25 vs 50 mT comparison.

## Prerequisites

- [ ] Stage 01 done; `MM_PER_PULSE = 0.001` is in the sketch.
- [ ] Sensor analog out → **2×10 kΩ ÷2 divider** → GPIO34. (0.1 µF cap optional.)
- [ ] Sensor powered from the **board's 5 V** (not the programmer), magnet on the carriage.
- [ ] In the Micronas tool, sensor **linearization left at default (raw)** — we
      are measuring the *uncorrected* curve. Only the **Magnetic Range** is set.
- [ ] `SWEEP_DISTANCE_MM` in the sketch fits your clear travel (default 9.0 mm).

## Wiring sanity check (do this first)

1. Upload the sketch, open Serial Monitor (115200, Newline).
2. Type `read`. You should get a stable millivolt value.
3. Jog the magnet a little (`500` = 0.5 mm) and `read` again — the value should
   **change**. If it doesn't move, or pins at 0 / max, fix wiring or the divider
   before sweeping.
   - Note the `sd` (noise) value. If it's large (tens of mV), that's the cue to
     add the 0.1 µF cap and/or raise `AVG_SAMPLES`.

---

## Running a sweep

1. Rest the carriage gently against the block, then nudge `200` away (backlash).
   This position is **travel = 0** for the sweep.
2. Type `sweep`. It steps away in 0.1 mm increments, averaging 200 reads per
   point, prints the CSV, then returns to start.
3. Copy everything between the `Distance_mm,Reading_mV,SD_mV` header and
   `# Sweep complete` and save it as a file in `data/` (see naming below).

> A Python logger that captures this automatically is the next build; for the
> first sweeps, Serial-Monitor copy-paste is fine.

## The range comparison

Run the sweep **at each range**, reprogramming the sensor in between:

1. Micronas tool → Sensor Settings → Customer Setup → **Magnetic Range = ±50 mT**
   → Write Customer Setup → Power On Reset.
2. Back to the sketch, `sweep`, save as `data/charac_50mT_run1.csv`.
3. Repeat for **±25 mT** → `data/charac_25mT_run1.csv`.
4. (Optional) a second run of each to check repeatability.

### What to look at

- **Near end (low mm):** is it a flat/clamped floor, or already climbing? A flat
  near end = clipping = blind zone for that range.
- **Slope:** steeper mV-per-mm = better resolution of small gape changes.
- **Monotonic:** the usable window must rise steadily (no flat or reversing
  regions) — that's the part we can linearize.

**Choose:** the most sensitive range that still climbs cleanly from travel 0
across the window. (Expected: 50 mT safe everywhere; 25 mT better slope if its
near end isn't clipped at your geometry.)

---

## Data file naming

```
data/charac_<range>_run<N>.csv      e.g. data/charac_50mT_run1.csv
```
Keep the header line in the file. Add the magnetic range and date as a comment
line at the top if your editor allows.

## Results — record here

| Range | File | Near end (clamped? from mm) | Slope (mV/mm, rough) | Usable window (mm) | Noise SD (mV) |
|-------|------|-----------------------------|----------------------|--------------------|---------------|
| 50 mT |      |                             |                      |                    |               |
| 25 mT |      |                             |                      |                    |               |

**Chosen range:** ____________  **Reason:** ____________
**Date / operator:** ____________

---

## Caveats

- Stay inside the travel; the sweep moves away from the block, so the only stall
  risk is the far end — make sure `SWEEP_DISTANCE_MM` leaves margin.
- The sensor must be **powered and outputting** during the sweep; if you only
  have it powered through the programmer, readings may be wrong.
- This curve is the **raw** (un-linearized) response. We linearize it in a later
  stage using these very measurements.

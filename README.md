# Oyster_gape

Non-contact measurement of oyster valve **gape** (how far the shells open and
close) using a Micronas **HAL 2425** Hall-effect sensor on one shell and a small
magnet on the other. As the gap changes, the sensor output changes. This repo
covers the sensor engineering: characterizing and **linearizing** the HAL 2425 so
its output reads linearly in millimetres, then a **three-sensor bench system**
(Board B) that shows live gape on a web dashboard, with a **per-unit tare** so
each unit reports true gape from its own closed baseline.

This is a clean restart. Nothing here is inherited unverified from prior work —
every constant is measured and documented.

---

## Demo

![Oyster gape dashboard demo](docs/demo.gif)

Three clothespin "oysters" on Board B's live dashboard — each shell opens in
proportion to the measured gape. Closed reads 0.00 mm (per-unit tare); the
slightest press registers immediately.
**[Watch the full video →](https://github.com/IamTemmy/Oyster_gape/releases/tag/v0.1-bench-demo)**
· more in [`docs/demo.md`](docs/demo.md).

---

## Current stage

**Bench system working — 25 mT + tare (Board B).** The full method is proven on
three clothespin "oysters" (pegs standing in for real animals):

- Sensors characterized and **linearized** (Micronas workflow, Procedure 03),
  first at 12 mT then re-done at **25 mT** (window 8.0–20.0) so the closed peg sits
  on the active ramp instead of in the dead zone.
- **Board B** (ESP32-WROVER) reads all three sensors (S1/S2/S3 → GPIO34/35/32, all
  ADC1) and serves a live **web dashboard** (`firmware/06_board_b_tare`).
- **Per-unit tare:** each unit records its own closed reading as a baseline and
  reports **true gape from closed** — `gape = (mV − baseline) / m`. The intercept
  cancels in that subtraction, so the per-unit mounting offset drops out. This is
  what lets one lab calibration serve many differently-mounted animals.

Per-sensor lines and the full method:
[`data/analysis/deployment_calibration.md`](data/analysis/deployment_calibration.md).
Watch it work: [demo](docs/demo.md).

**Next (deployment):** re-run the same method at the range chosen for real oysters
(see decisions log), add a mounting jig for consistent closed standoff, and
flash-persist the baselines so a tare survives power cycles.

---

## Hardware facts (current rig)

| Item | Value |
|------|-------|
| Sensor | Micronas HAL 2425 (16 linearization setpoints) |
| Programmer / software | HAL-APB on COM-port, HAL/HAR 24xy Programming Environment (SW v3.04) |
| Controller | ESP32-WROVER (Freenove) |
| Driver | Geckodrive G251 — **fixed 10 microsteps/full step → 2000 pulses/rev** |
| Motor | 200 full-step |
| Magnet | 3/8" dia × 1/8" thick, N40 neodymium, axially magnetized |

### Pin map

| Signal | ESP32 | Notes |
|--------|-------|-------|
| STEP | GPIO18 | → G251 Terminal 10 |
| DIR  | GPIO19 | → G251 Terminal 9 |
| Sensor analog in | GPIO34 | ADC1_CH6, input-only. **Not** GPIO4 (that is ADC2, WiFi-conflicting) |
| Common ground | GND | shared with G251 Terminal 12 |

### Analog front end (sensor output → ESP32) — DECIDED: ÷1.5, no cap

- **Divider: ÷1.5** (two 10 kΩ in parallel = 5 kΩ on top, one 10 kΩ to ground).
  The Stage 02 2×2 test showed ÷1.5 and ÷2 back-scale to the same true curve
  within 0.3% — ÷1.5 does **not** clip with this magnet/geometry — while ÷1.5
  spans ~1.33× more mV at the pin, giving more ADC resolution. (Re-check if a
  future geometry brings the magnet much closer and raises the output.)
- **No filter cap.** No-cap noise is ~6 mV SD → ~0.45 mV after 200-sample
  averaging — already excellent. On this ÷1.5 setup the cap **reproducibly
  raised** noise (~21 mV SD, confirmed on a clean retest — not a loose
  connection). Cause not pinned down (likely the specific cap interacting with
  the breadboard/ADC), but irrelevant: the cap isn't needed and hurts here, so
  it's dropped. See data/analysis/.
- Decouple the sensor's 5 V supply with 100 nF + ~4.7 µF at its pins
  (output is ratiometric, so supply ripple = error).

---

## Key decisions log

- **Noise:** keep the ESP32 (per advisor); attack noise in firmware via
  oversampling + averaging + settle time, on ADC1. External ADC (e.g. ADS1115)
  held as a fallback only.
- **Analog front end:** **÷1.5 divider, no cap** (Stage 02 2×2 test). ÷1.5
  proven not to clip (back-scales to ÷2 within 0.3%) and gives more resolution;
  cap unneeded because 200-sample averaging drives noise to ~0.5 mV.
- **Magnetic range:** ±3 mT is **blind below ~12 mm** with this magnet (near field
  clips); higher ranges reach smaller gaps. **Bench: ±25 mT** — chosen and
  bench-proven (window 8.0–20.0) because it places the peg's closed gap on the ramp
  without pushing the window start into the steep shoulder (the F:000000 spike).
  **Deployment: likely ±3 mT** for real (larger) oysters — the professor's lean,
  for its longer operating range at larger separations. The *method* is
  range-independent (linearize → mount so closed is on-ramp → tare), so deployment
  re-runs it at the final range once the oyster geometry is known.
  *(Updated 2026-07-05; earlier this read "final choice pending the measured gape window.")*
- **Distance constant:** measured = **0.001 mm/pulse** (1000 pulses/mm,
  2.0 mm/rev), Procedure 01.
- **Output polarity:** sensor output **falls** with increasing gap
  (closed = high mV ~3.6 V, open = low mV). Kept on purpose: it places the
  closed→just-opening transition in the high-signal, high-resolution region —
  the event the research most cares about. A falling curve linearizes exactly
  like a rising one. Deployment caveat: "fully open" and "unpowered / broken
  wire" both read low — rely on the HAL's wire-break detection to disambiguate.
- **Per-sensor calibration:** every sensor is characterized and linearized
  **individually** — setpoint files are NOT shareable between units (intrinsic
  part spread + mounting variation from bent pins + magnet pairing). The process
  is identical each time and the high-level settings (range, output mapping) are
  shared; only the sweep + setpoints + a per-unit 2-point calibration are
  repeated. The 2-point cal is what absorbs mounting-distance variation. A sensor
  must be characterized in (or in a jig replicating) its final mounting geometry.
  Future shortcut to validate empirically: if several sensors' curve *shapes*
  match, share one curve + per-unit 2-point cal only.
- **LOCK is irreversible.** Locking a sensor can never be undone — every
  programming session ends with an explicit LOCK checklist before that button.

---

## Conventions

- One firmware sketch per folder (Arduino requirement), numbered by stage.
- Every stage has a matching `docs/procedures/NN_*.md` with a results table
  filled in from the real run.
- Raw data lands in `data/` with the sketch/run that produced it named in the file.

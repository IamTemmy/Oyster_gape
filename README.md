# Oyster_gape

Non-contact measurement of oyster valve **gape** (how far the shells open and
close) using a Micronas **HAL 2425** Hall-effect sensor on one shell and a small
magnet on the other. As the gap changes, the sensor output changes. This repo
covers the first engineering stage: **calibrating and linearizing the sensor so
its output reads linearly in millimetres of gape.**

This is a clean restart. Nothing here is inherited unverified from prior work —
every constant is measured and documented.

---

## Repo structure

```
Oyster_gape/
  firmware/        ESP32 sketches (one folder per sketch)
    01_leadscrew_calibration/
  host/            Python tools that talk to the ESP32 (loggers, plotting)
  hardware/        Schematic, wiring notes, photos
  docs/
    procedures/    Step-by-step procedures, one per stage
  data/            Raw captures (CSV) + their plots
  README.md
```

## Current stage

**02 — Characterization sweep.** Leadscrew calibrated (**0.001 mm/pulse**,
Procedure 01). Now sweeping distance vs sensor output to (a) pick the analog
front end (÷1.5 vs ÷2 divider, cap vs no cap) and (b) select the magnetic range.
See `docs/procedures/02_characterization_sweep.md`.

Next: range selection → per-sensor 2-point calibration → 16-setpoint
linearization in the Micronas tool → verification.

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

### Analog front end (sensor output → ESP32)

- **Under test (Stage 02): ÷1.5 vs ÷2 divider, cap vs no cap (a 2×2).**
  Measured sensor output max is ~3.6 V (not the ~4.65 V worst case), so even
  ÷1.5 keeps the pin ~2.4 V — inside the ADC's good range, no clipping. ÷1.5
  (two 10 kΩ in parallel = 5 kΩ on top, one 10 kΩ to ground) keeps more
  resolution; ÷2 (two 10 kΩ in series) is the safe alternative. The 2×2 decides.
- **0.1 µF** (= 100 nF) filter cap from the ADC node to ground, at the pin —
  effectiveness measured against the no-cap runs (the SD band).
- Decouple the sensor's 5 V supply with 100 nF + ~4.7 µF at its pins
  (output is ratiometric, so supply ripple = error).
- Measured noise so far: ~9 mV SD on a ~2500 mV signal (~0.35%) — already low.

---

## Key decisions log

- **Noise:** keep the ESP32 (per advisor); attack noise in firmware via
  oversampling + averaging + settle time, on ADC1. External ADC (e.g. ADS1115)
  held as a fallback only.
- **Magnetic range:** the prior workflow used ±3 mT, but with the current magnet
  ±3 mT is **blind below ~12 mm** (the near field clips). For a small-mm gape
  window the data points to **±25 mT** (usable from ~2 mm) or **±50 mT** (from
  ~0 mm). Final choice pending the measured gape window. Decided per-data, not
  inherited.
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

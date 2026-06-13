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

**01 — Leadscrew calibration.** Measuring mm-per-pulse so sweeps report true
distance. See `docs/procedures/01_leadscrew_calibration.md`.

Next: characterization sweep (distance vs ADC, with oversampling/averaging) →
range selection → 2-point calibration → 16-setpoint linearization in the
Micronas tool → verification.

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

- Voltage divider **÷2** using two 10 kΩ resistors (sensor OUT → node → GND),
  keeping the ≤4.65 V output under ~2.4 V at the pin.
- Optional **0.1 µF** (= 100 nF) filter cap from the ADC node to ground, at the
  pin. Not required to start — firmware oversampling/averaging does the main
  noise reduction.
- Decouple the sensor's 5 V supply with 100 nF + ~4.7 µF at its pins
  (output is ratiometric, so supply ripple = error).

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
- **Distance constant:** measured, not assumed (Procedure 01).
- **LOCK is irreversible.** Locking a sensor can never be undone — every
  programming session ends with an explicit LOCK checklist before that button.

---

## Conventions

- One firmware sketch per folder (Arduino requirement), numbered by stage.
- Every stage has a matching `docs/procedures/NN_*.md` with a results table
  filled in from the real run.
- Raw data lands in `data/` with the sketch/run that produced it named in the file.

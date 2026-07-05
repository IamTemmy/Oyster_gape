# Deployment calibration — Oyster_gape (MBRACE)

> **Current working approach: 25 mT + firmware tare** (2026-07-05, sketch
> `firmware/06_board_b_tare`). The 12 mT section below is retained for its offset
> findings and is **superseded** for the bench.

## Why we moved off 12 mT — the two-rulers / stacked-offset finding

The "distance" axis in every rig CSV is **carriage travel from the datum block**, not the
true magnet-to-sensor gap. The rig sweep starts with the magnet already standing off the
sensor — **measured ~5 mm** on the carriage — so **every CSV distance is ~5 mm short of the
true rig gap**. The peg mount then stacks its OWN, different offset on top (worked backward
from S3: 330 mV closed at a 12.4 mm caliper gap ⇒ ~1.5 mm peg offset). These offsets are
(a) not equal and (b) not the same sign, so **no single constant correction fixes both
setups** — which is what fooled the "peg is wider than the dead zone on paper" check twice.

Consequence at 12 mT: the window started at rig 11.0 mm and the closed peg landed just
below it → clamped ~300 mV (dead zone); small openings didn't register until ~1 mm in.

## Fix: 25 mT re-linearization (per-sensor, window 8.0–20.0)

Higher magnetic range shifts the active region toward **smaller** gaps, so at 25 mT the
window can start low (8.0) without hitting the steep shoulder / F:000000 spike. Re-done on
fresh pegs + fresh sensors. Bench-verified: closed pegs read **~448–464 mV (~8.5 mm, on
ramp)** and a realistic full-open reads **~2510 mV (~17.9 mm, on ramp)** — both gates passed
(closed off the floor, full-open under the ceiling).

| Channel | gape_mm line (abs) | Ramp (mm) | Floor / ceil (mV) | Linearity |
|:--:|:--:|:--:|:--:|:--:|
| **S1 → GPIO34** | (mV + 1368.4) / 214.7 | ~8.6–20.5 | 312 / 3150 | 1.5% |
| **S2 → GPIO35** | (mV + 1386.4) / 217.7 | ~8.3–20.3 | 284 / 3150 | 1.2% |
| **S3 → GPIO32** | (mV + 1355.5) / 216.9 | ~8.4–20.3 | 296 / 3150 | 1.7% |

Source: `data/sim_test_data/charac_S{1,2,3}_25mT_linearized_run1.csv`, window 8.0–20.0.
Slopes agree within ~1.4% (same tight clustering as 12 mT).

## The tare — what makes readings TRUE GAPE

Board B reports **gape = opening from closed**, not absolute separation:

    gape_mm = (mV − baseline_mV) / m

`baseline_mV` is each sensor's CLOSED reading, captured live. **The intercept b cancels in
the subtraction**, so gape is immune to the per-unit mounting offset — every unit zeroes to
itself and reads 0.00 mm closed. This is the mechanism that lets one lab calibration serve
many differently-mounted animals: linearize once, mount so closed sits on the ramp, tare
per unit at install.

- **Baseline capture:** automatic ~1.5 s after boot (hold pegs closed), **gated to on-ramp
  readings** — an off-ramp/clamped sensor is refused, not zeroed to junk. Re-baseline anytime
  via the serial `zero` command or the dashboard **Zero** button (needed after re-seating a
  peg or separating the magnet/sensor halves).
- **"Closed" band:** within **±0.05 mm** of baseline reads as closed (≈ noise floor, well
  below any real gape) so closed is steady, not flickering; any real press clears it instantly.
- Baselines live in RAM (re-captured each boot / on `zero`). **Deployment TODO:** persist to
  flash so they survive a power cycle (Phase 2).

## Operating assumptions (stated, not proven)
- **Hinge-arc error is minimal** (professor's ruling for this oyster size/opening). The
  peg/oyster opens on an arc, not the carriage's straight line, so rig-calibrated mm are not
  *exactly* true mouth-gape mm at wide openings. The tare removes the *offset*; it does **not**
  remove this *arc-scale* error. Accepted assumption; revisit if a future animal opens wider.
- **Match the magnet:** the tare cancels offset, not slope. The magnet used at linearization
  must match the deployed magnet, or gape is scaled wrong.

## Deployment range (real oysters) — later
Professor prefers **3 mT** for large oysters (longest operating range, active region at
larger gaps). The *method* proven here (linearize → mount so closed is on-ramp → tare) is
identical at any range; deployment re-runs it at the chosen range once the oyster geometry
is known. 25 mT is the bench range only.

---

## Earlier: 12 mT bench (SUPERSEDED — retained for the offset findings)


All 3 peg sensors are linearized at **12 mT** on the rig (Board A), window
**11.0–24.0 mm**, SCALE 1024/29696, OUT 10/90, Extrapolation OFF. Each sensor keeps
its OWN mV->mm line for Board B:

    gape_mm = (mV - b) / m        (m = slope mV/mm, b = intercept mV)

**Label each physical sensor to match its Board B channel** (write it on the sensor):
- **S1 -> GPIO34**
- **S2 -> GPIO35**
- **S3 -> GPIO32**

| Sensor | Cal pt1 | Cal pt2 | Verify ramp (mm) | Linearity | Slope m (mV/mm) | Intercept b (mV) | Clamps lo/hi (mV) |
|:------:|:-------:|:-------:|:----------------:|:---------:|:---------------:|:----------------:|:-----------------:|
| S1 | 10796 | 2380 | 11.4–24.4 | 2.4% | 199.8 | -1810 | 299/3150 |
| S2 | 10156 | 2340 | 11.5–24.6 | 2.4% | 197.8 | -1799 | 317/3150 |
| S3 | 10720 | 2404 | 11.7–24.5 | 1.9% | 200.6 | -1863 | 316/3150 |

*(m, b) are computed by fitting the ramp region of each sensor's verify CSV. These
three lines go directly into Board B's firmware — one per channel.*

## Notes
- All three use the SAME range/window because the pegs are identical (~12 mm / 1.2 cm
  face-to-face closed, measured).
- If any sensor's transfer curve spikes at start, nudge the window start out (as we did
  tuning ranges) and re-note the window for that sensor.
- Board B has its own ADC + resistors, so m/b may shift slightly there; a 2-point check
  on Board B at a known closed/open gap will trim any offset (Phase 2 refinement).

---

## Board B conversion lines (FINAL — all 3 sensors linearized & verified)

| Channel | gape_mm = ... | Valid ramp (rig frame) | Low clamp (mV) |
|:-------:|:-------------:|:----------------------:|:--------------:|
| **S1 -> GPIO34** | (mV + 1810) / 199.8 | ~11.4–24.4 mm | ~299 |
| **S2 -> GPIO35** | (mV + 1799) / 197.8 | ~11.5–24.6 mm | ~317 |
| **S3 -> GPIO32** | (mV + 1863) / 200.6 | ~11.7–24.5 mm | ~316 |

`gape_mm` is distance in the **rig datum frame** (carriage travel from the block during
`verify`) — NOT the true magnet–sensor air gap and NOT the peg face-to-face gap. They
differ by a fixed mounting offset (the magnet + sensor bodies protrude into the peg gap),
so a peg *face* gap of X mm maps to a *smaller* rig-frame distance.

Outside the valid ramp the output is pinned at the clamp band (~300 mV low / ~3150 mV
high) and carries NO distance information — flag it (CLAMP); do not trust the mm.

### Bench finding (2026-07-03) — closed peg sits in the LOW clamp
**CORRECTION** to an earlier version of this file, which claimed the closed peg reads a
mid-ramp ~684–728 mV (OK). That was wrong: it was computed by plugging `mm = 12.7`
straight into the line, assuming peg-face-gap = rig-frame distance. It is not.

Ground truth (`data/charac_S{1,2,3}_12mT_linearized_run1.csv`): the ramp is flat at the
~300 mV LOW clamp from 0 out to ~11 mm (rig frame), then rises. On Board B a *closed*
bench peg (~12 mm / 1.2 cm face-to-face) reads **~300–335 mV on all three channels — the
LOW clamp**, i.e. at/just below the window floor, because the mounting offset maps it to
≤~11 mm rig-frame. A valid live gape reading therefore begins once a peg **opens** past
the window start; the opening excursion reads correctly on the ramp (bench-confirmed:
~979→1922 mV as a peg was pressed open).

Implication — with the current 12 mT window the CLOSED baseline is not directly readable
(clamped). To read it, either (a) lower the window start and re-linearize (mind the
steep-shoulder F:000000 spike rule), (b) increase the rest gap / recess the magnet+sensor
so closed lands inside the existing window, or (c) accept closed-as-clamped and read only
the opening excursion — fine for the bench proof.

### Consistency / scaling note
- Slopes agree within **1.4%**; linearity 1.9–2.4%. Mounting is repeatable and the parts
  are well matched.
- The main variation is in the **intercept (offset)** — at a common rig-frame distance
  the three lines differ by ~0.2 mm (b spans −1799 to −1863 mV). This is why per-sensor
  lines are kept: a single shared line would carry up to ~0.2 mm error across the ramp.
- **Scaling implication:** because the *shape* (slope) is so consistent and only the
  *offset* varies, at deployment scale you likely won't need a full 16-point
  linearization per unit — a full linearization on a few reference units plus a quick
  **single-point offset check** per deployed unit would capture the per-unit variation.
  (Deployment-scaling idea, not a Phase-1 action.)

**Status: linearization COMPLETE for all three peg sensors (S1/S2/S3) at 12 mT.**

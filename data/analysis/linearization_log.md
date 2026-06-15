# Stage 03 — Linearization results log (per range)

Magnet: 3/8" x 1/8" N40 (fixed). Front end: ÷1.5, no cap. OUT_MIN/MAX = 10/90.

| Range | Window (mm) | Cal pt1 | Cal pt2 | Result |
|-------|-------------|---------|---------|--------|
| 400 mT | 1.0–10.0 | 3004 | 408 | REJECTED — "small range between calibration points" |
| 400 mT | 0.5–25.0 | 3444 | 64 | REJECTED — swing too small even widened. **400 mT not linearizable with this magnet.** |

## Notes
- 400 mT is the least sensitive range; its internal signal swing over the usable
  span is too small for the 2-point calibration to anchor. The tool itself flags
  it and suggests lowering the magnetic range — consistent with the Stage 02
  sensitivity analysis (400 mT had the smallest swing of all eight).
- Expectation: 200 mT likely the same; the sensitive ranges (50 mT and down) are
  the real candidates.

| 200 mT | 1.0–10.0 | 5940 | 808 | Calibrated OK, but linearization transfer spiked (>1000% FS) — unwritable (F:000000). |
| 200 mT | 4.0–14.0 | 2704 | 456 | REJECTED — pushing start out to tame the spike drops swing below calibration minimum. |
| 100 mT | 1.0–10.0 | 11932 | 1660 | Calibrated OK, but transfer spiked (~700% FS) — unwritable. Retesting with pushed-out start. |

## Finding (400 + 200 mT)
The two least-sensitive *non-clipping* ranges fail to linearize with this magnet,
for two linked reasons:
- **Swing too small** for the 2-point calibration (400 mT outright; 200 mT once
  the window is pushed out).
- **Steep first segment** (curve is steepest right at the block on non-clipping
  ranges) makes the linearizer's inverse spike (>1000% FS) → setpoints unwritable.
There is no window that gives both enough swing AND a gentle-enough start.
=> 400 mT and 200 mT are documented as IMPRACTICAL with the current magnet.
Next: confirm whether 100 mT (more swing) escapes the squeeze with a pushed-out start.

| 100 mT | 4.0–14.0 | 5404 | 916 | **SUCCESS** — smooth transfer, written, verified. Ramp straight to 2.6% (max 75 mV dev on 2900 mV ramp), slope ~260 mV/mm. |

## Method confirmed
The fix for non-clipping ranges: **start the window past the steep first segment**
(4 mm for 100 mT). 1 mm spiked (unwritable); 4 mm gave a clean flat–ramp–flat
verified at 2.6% linearity. 100 mT is the first fully linearized + verified range.

| 50 mT | 3.0–13.0 | 13636 | 2076 | Transfer spiked — start still too steep. |
| 50 mT | 6.0–16.0 | (recal) | | **SUCCESS** — smooth transfer, written, verified. Ramp straight to **1.4%** (max 39 mV dev), slope 258 mV/mm. Cleanest range so far; front-runner. |

## Linearization-safe window starts (corrected, per range)
Each range needs its window START pushed past its steep shoulder (further out as
sensitivity rises), or the linearizer's inverse spikes and setpoints won't write:
- 100 mT -> start 4.0 mm  (verified, 2.6%)
- 50 mT  -> start 6.0 mm  (verified, 1.4%)
- 25 mT  -> testing ~8.0 mm
(200/400 mT impractical: can't get swing + gentle start simultaneously.)

| 25 mT | 8.0–20.0 | (ok) | | **SUCCESS** — verified, ramp straight to 2.0%, slope 217 mV/mm, 8.5–20.7 mm. Wider window than 50mT. |

| 12 mT | 11.0–24.0 | (ok) | | **SUCCESS** — verified, see straightness in session. Window 11–26 mm. |

| 6 mT | 17.0–32.0 | (ok) | | **SUCCESS** — verified. Window 17–37 mm; widest span yet before 3mT. |

| 3 mT | 20.0–44.0 | (ok) | | First try bent at top (5.8%) — END reached the flat zone. Re-run accepted at 3.1%. |

---

# STAGE 03 — FINAL RESULTS (6 ranges linearized + verified)

| Range | Window start→end (mm) | Width (mm) | Slope (mV/mm) | Linearity (max dev) |
|------:|:---------------------:|:----------:|:-------------:|:-------------------:|
| 3 mT   | 21.8–43.7 | 21.9 | 115 | 3.1% |
| 6 mT   | 18.2–36.4 | 18.2 | 140 | 2.6% |
| 12 mT  | 11.4–25.2 | 13.8 | 187 | 2.3% |
| 25 mT  | 8.5–20.7  | 12.2 | 217 | 2.0% |
| 50 mT  | 6.4–16.4  | 10.0 | 258 | 1.4% (cleanest) |
| 100 mT | 4.1–14.2  | 10.1 | 260 | 2.6% |
| 200 mT | — | — | — | IMPRACTICAL (swing vs gentle-start squeeze) |
| 400 mT | — | — | — | IMPRACTICAL (swing too small to calibrate) |

## Key findings
1. **Six usable linearized ranges (3–100 mT)**, each a straight gap-vs-output
   ramp, all within 1.4–3.1% linearity. Together they form a "range ladder":
   pick the range whose ramp covers a given oyster's gape window.
2. **Window rule:** each range's window must START past its steep shoulder
   (further out as the range gets more sensitive) and END before its flat tail —
   else the linearizer's inverse spikes (unwritable) or the ramp bends.
3. **Magnet reach limit ≈ 38 mm.** Going to the most sensitive range (3 mT) did
   NOT extend usable reach much beyond 6 mT — the field flattens by ~38 mm
   regardless of range. The largest gapes would need a stronger magnet.
4. **200/400 mT impractical** with this magnet: too little signal swing to both
   calibrate and linearize.
5. Measurement quality: more-sensitive ranges trade slope (resolution) for reach.
   50 mT is the cleanest/highest-resolution general-purpose choice.

## Open / next
- Deployment: pick range per oyster from its actual gape window (closed+open mm).
- Set Clamp-Low/High band + wire-break detection (currently 0/100).

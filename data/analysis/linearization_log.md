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

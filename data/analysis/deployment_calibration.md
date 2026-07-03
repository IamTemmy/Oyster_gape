# Deployment calibration — per-sensor 12 mT (bench simulation)

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
- All three use the SAME range/window because the pegs are identical (~12.7 mm closed).
- If any sensor's transfer curve spikes at start, nudge the window start out (as we did
  tuning ranges) and re-note the window for that sensor.
- Board B has its own ADC + resistors, so m/b may shift slightly there; a 2-point check
  on Board B at a known closed/open gap will trim any offset (Phase 2 refinement).

---

## Board B conversion lines (FINAL — all 3 sensors linearized & verified)

| Channel | gape_mm = ... | Closed peg (12.7 mm) reads | Valid ramp |
|:-------:|:-------------:|:--------------------------:|:----------:|
| **S1 -> GPIO34** | (mV + 1810) / 199.8 | ~728 mV | ~11.4–24.4 mm |
| **S2 -> GPIO35** | (mV + 1799) / 197.8 | ~713 mV | ~11.5–24.6 mm |
| **S3 -> GPIO32** | (mV + 1863) / 200.6 | ~684 mV | ~11.7–24.5 mm |

Outside the valid ramp, clamp/flag the reading (shell beyond the calibrated window).
All three closed-peg readings sit on the ramp (well above the ~300 mV low clamp), so a
closed peg gives a live reading, not a clamped one.

### Consistency / scaling note
- Slopes agree within **1.4%**; linearity 1.9–2.4%. Mounting is repeatable and the parts
  are well matched.
- The main variation is in the **intercept (offset)** — the closed-peg readings span
  ~684–728 mV (~0.22 mm). This is why per-sensor lines are kept: a single shared line
  would carry up to ~0.22 mm error at the closed end.
- **Scaling implication:** because the *shape* (slope) is so consistent and only the
  *offset* varies, at deployment scale you likely won't need a full 16-point
  linearization per unit — a full linearization on a few reference units plus a quick
  **single-point offset check** per deployed unit would capture the per-unit variation.
  (Deployment-scaling idea, not a Phase-1 action.)

**Status: linearization COMPLETE for all three peg sensors (S1/S2/S3) at 12 mT.**

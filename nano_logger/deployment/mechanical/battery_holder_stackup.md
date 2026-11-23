# Battery Holder — 2019 Stack-Up Error and Fix

**Date:** 2026-11-22
**Part:** `Sensors Battery Container (9).stl`, dated 2019-06-29, from
`OLD MBRACE DOCs (Pre-Fall 2023)/Underwater/`
**Fix:** [`battery_container_72.5mm.stl`](battery_container_72.5mm.stl)

---

## Summary

The 3-cell 18650 holder used by this project since 2019 has an internal cavity
**2.5 mm too short**. At worst-case cell tolerance the retaining spring is
**coil-bound — zero travel** — so it acts as a rigid spacer rather than a spring
and provides no compliant contact force.

The result is intermittent, position-dependent cell contact: whether a given
slot conducts depends on glue thickness, print tolerance and how the spring
happens to seat. Cells also jam on insertion because they are being pressed
against a solid stack.

**This is a design error, not an assembly or print error.** Every holder built
from this file has it.

---

## Measurements

Cavity measured by ray-casting the STL (3,671 sample points through both end
plates):

| Feature | Original | Corrected |
|---|---|---|
| **Internal cavity length** | **70.00 mm** (min 69.99, max 70.00) | **72.50 mm** |
| End plate thickness | 3.17 mm each | 3.17 mm (unchanged) |
| Envelope | 42.08 × 76.34 × 36.69 mm | 42.08 × **78.84** × 36.69 mm |

A printed sample measured **70.20 mm** against the designed 70.00 — the printer
is accurate; the design is the problem.

### Component stack

| Component | mm | Source |
|---|---|---|
| Cell, maximum tolerance | 65.25 | [Samsung INR18650-35E datasheet](https://cdn.shopify.com/s/files/1/0613/3327/4870/files/35eDatasheet.pdf) |
| Spring at solid height | 2.67 | measured |
| Contact plate | 1.04 | measured |
| Spring mounting plate | 1.04 | measured |
| **Total** | **70.00** | |

That sum is *exactly* the designed cavity. Spring free height is 8.62 mm, so
usable travel is 5.95 mm — and **none of it is available**:

| Cell length | Spring travel, original | Spring travel, corrected |
|---|---|---|
| 65.25 mm (max tolerance) | **0.00 mm** | **3.50 mm** |
| 65.00 mm (nominal) | 0.25 mm | 3.25 mm |

---

## The fix

Target 3.5 mm of compression at worst-case cell length:

```
65.25 (cell) + 1.04 (contact plate) + (8.62 - 3.5) (spring under load)
            + 1.04 (spring plate)  =  72.45 mm  ->  72.50 mm
```

That leaves 1.6 mm of margin before the spring goes solid — room for glue
thickness, print variation and a long cell.

**Implementation:** the corrected STL was produced by translating every vertex
beyond the mid-length plane by +2.5 mm along the prism axis. The body is
prismatic from y≈5 to y≈73, so this lengthens it exactly, leaves the topology
untouched, and keeps **every other feature bit-identical** to the 2019 part —
cell bores, end plate geometry, contact-plate mounting holes, external profile.
Existing springs, plates and wiring transplant directly.

Verified after the edit: cavity 72.50 mm across 1,887 ray samples
(min 72.49, max 72.50), watertight, single body.

---

## How it was found

Two days of intermittent battery contact, misdiagnosed several times along the
way. The evidence that converged on it:

1. **Parallel cells sitting at different voltages.** Cells tied in parallel
   cannot differ. Adding a 3.70 V cell to two at 4.132 V left the holder output
   unchanged at 4.132 V, where the resistive average predicts 3.99 V — that
   cell was electrically absent. A two-cell control case gave 3.92 V against a
   predicted 3.915 V, confirming the method.

2. **A full run on one cell.** In a 3.5 h run, cell 1 fell 4.126 → 4.114 V
   (≈52 mAh at 4.3 mAh/mV) while cells 2 and 3 did not move at all. One cell
   carried the entire load; the other two were never connected.

3. **Bus continuity passed throughout.** Wire-to-plate and wire-to-spring
   continuity beeped on every slot, every time — because that test measures
   metal to metal and skips the only interface that was failing: plate-to-cell.

4. **The cavity measured correct at 70.20 mm against a 70.00 mm design.** Only
   when the component stack was added up did the zero-travel condition appear.

**Lesson for the next person:** continuity to a contact plate does not test
whether the plate touches the cell. Test each slot with a single cell and read
the output voltage — that is the only check that exercises the real interface.

---

## Verification after rebuild

All three slots of the corrected holder passed:

1. **Presence** — one cell at a time in each slot, output reads that cell's
   voltage. All three pass.
2. **Load** — cells seat against a definite spring push-back and release
   without force.
3. **Sustained** — 28.3 min logging run, zero write errors, zero lost samples
   (see [`../docs/power_chain_bisection.md`](../docs/power_chain_bisection.md)).

### Slot test procedure (keep using this)

| Test | Method | Pass |
|---|---|---|
| Presence | single cell, one slot at a time | output = that cell's voltage |
| Resistance | 1 A through the charger, measure drop at the output | 30–60 mV |
| Intermittence | tap, press each cell, flex leads, invert, under load | no flicker |

---

## Open items

- The glue-mounted contact plates remain a weak point. A future revision should
  capture them mechanically rather than relying on adhesive, and add probe ports
  at each cell position so an individual slot can be metered in place.
- Second holder to be printed from the corrected file.

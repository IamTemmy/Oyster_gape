# Procedure 04 — Board B: live readout, dashboard, and tare

**Goal:** read the linearized sensors on a second ESP32 (Board B) and report
**true gape** — the shell opening from closed — on a serial readout and a live web
dashboard.

**Sketches (Board B, one folder each):**
- `firmware/04_board_b_readout/` — 3-sensor serial readout (bench debug)
- `firmware/05_board_b_dashboard/` — WiFi web dashboard (absolute mm)
- `firmware/06_board_b_tare/` — **current**: 25 mT + tare, reports true gape

**Calibration source:** `data/analysis/deployment_calibration.md` (per-sensor `m`,
`b`, ramp, clamp floors).

---

## Hardware (Board B — no motor)

| Signal | ESP32 | Notes |
|--------|-------|-------|
| S1 in | GPIO34 | ADC1_CH6 |
| S2 in | GPIO35 | ADC1_CH7 |
| S3 in | GPIO32 | ADC1_CH4 |

- **All three on ADC1.** ADC2 shares hardware with the WiFi radio, so ADC2 reads
  fail once the dashboard's WiFi is up. ADC1 always works.
- One **÷1.5 divider per sensor** (2×10 kΩ parallel on signal + 1×10 kΩ to GND);
  do not share divider legs. Same front end as the rig.
- USB-powered (Phase 1). Battery/power is Phase 2 — deliberately not built in.

## Which sketch to flash

- **06 is the current build** — flash it for the demo and for true-gape readings.
- 04 (serial only) and 05 (dashboard, absolute mm) are kept as references / bench
  debug; 06 supersedes both.

## WiFi (05 / 06)

- Credentials + mode live in `secrets.h` (**gitignored**). Copy
  `secrets.example.h` → `secrets.h` in the sketch folder and fill it in.
- `NET_MODE`: `NET_AP` (Board B makes its own network → `http://192.168.4.1`) or
  `NET_STA` (join existing WiFi → IP printed on serial at boot). AP is the more
  reliable demo path; some institutional networks isolate clients and block the
  laptop from reaching Board B even when both are "connected."

---

## Before tare — the two gate checks

The tare only works if closed sits **on the ramp**. After mounting, read the raw
mV (serial `read`, or the dashboard footer) and confirm both:

1. **Closed peg on the ramp** — a mid-ramp mV, not the ~300 mV floor (CLAMP-LO).
2. **A realistic full-open still on the ramp** — under the ~3130 mV ceiling, not
   CLAMP-HI / "above window".

If closed clamps low, the window is too high for this mounting → re-linearize lower
(Procedure 03) or increase the mounting standoff. If full-open clamps high, the
opening range exceeds the window → widen it or drop to a lower range. (For the 25 mT
peg build these both passed: closed ~450 mV / ~8.5 mm, full-open ~2510 mV / ~17.9 mm.)

## Tare workflow (06)

    gape = (mV − baseline) / m

The intercept `b` cancels in that subtraction, so the per-unit mounting offset drops
out — every unit zeroes to itself and reads 0.00 mm closed.

- **Auto-capture at boot:** ~1.5 s after power-on the firmware captures each
  sensor's closed reading as its baseline. **Hold the pegs CLOSED through boot.**
- **On-ramp gate:** an off-ramp / clamped reading is **refused**, not zeroed to
  junk (shows "No baseline"). Close the peg and re-zero.
- **Re-baseline anytime:** serial `zero`, or the dashboard **Zero** button — needed
  after re-seating a peg or separating the magnet/sensor halves.
- **"Closed" band:** within ±0.05 mm of baseline reads as closed (≈ the noise
  floor), so closed holds steady; any real press registers immediately.
- **Gauge auto-scales** to each unit's physical max opening (`rampHi − baseline`) —
  no fixed full-scale constant.

---

## Sketch command reference (serial, 115200)

```
zero    re-capture the closed baseline (hold pegs closed first)   [06]
read    one-shot verbose read — absolute mm + gape                [04 / 06]
help    menu
```

The live readout is the web dashboard; serial is for setup / debug.

## Records to keep

Per unit: sensor ID → channel, the 25 mT `(m, b)` line used, the closed baseline mV
captured at install, and a caliper spot-check of gape at one open position.

## Notes

- Baselines live in RAM (re-captured each boot / on `zero`). **Deployment TODO:**
  persist to flash so a tare survives a power cycle.
- The tare removes **offset**, not **slope** — the magnet used at linearization must
  match the deployed magnet, or gape is scaled wrong.
- Hinge-arc error (the peg/oyster opens on an arc, not the rig's straight line) is
  taken as **minimal** per the professor; the tare does not correct it. Revisit if an
  animal opens much wider. See `data/analysis/deployment_calibration.md`.

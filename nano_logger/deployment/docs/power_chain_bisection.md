# Power Chain Bisection — Finding the Source of Silent Data Loss

**Date:** 2026-11-21 → 2026-11-23
**Outcome:** fault isolated to the **BMS**. Logger, SD path, card and MT3608 all
proven clean across 112 minutes of logging.

---

## Summary

| Configuration | Duration | Samples | `wr_err` | Verdict |
|---|---|---|---|---|
| E3631A 5.00 V → Nano `5V` pin | 46.6 min | 1…27,969 | **0** | logger, SD module, card good |
| E3631A 3.80 V → **MT3608** → Nano | 36.7 min | 1…22,000 | **0** | **MT3608 good** |
| Battery + **BMS** → MT3608 → Nano | 56.8 min | →34,038 | **103** | **fault** |
| Battery, **no BMS** → MT3608 → Nano | 28.3 min | 1…16,970 | **0** | **chain clean** |

Every clean run held 9.9968 Hz with zero sequence breaks and no step over
101 ms. The BMS run averaged **1.8 write errors per minute**; the no-BMS run of
28 minutes would have predicted ~51 errors and produced none.

---

## The failure mode

Two bench runs "died" after 11 and 46 minutes. They had not died.

Cell 1 fell 4.126 → 4.114 V over the 3.5 h wall clock — about **52 mAh** at the
measured 4.3 mAh/mV. At 14 mA that is **3.7 hours of draw**. The 46 minutes of
logging only accounts for ~11 mAh, **a fifth of the charge actually consumed**.

The Nano never lost power. **It went deaf.**

An SD card that sees a supply interruption mid-write can lock up and refuse all
subsequent writes until it is power-cycled. The firmware checked neither
`println()` nor `flush()`, so the loop went on sampling, timestamping and
writing into nothing — at full current, with no indication anything was wrong.

**In a 30-day deployment that is a silent total loss:** both packs flattened,
46 minutes of data recovered, and nothing to say why.

---

## Firmware fix (`cb6a35d`)

Every row is now verified with `getWriteError()`. On failure the card is
re-initialised and the log reopened in append mode, rate-limited to once per 5 s
so a genuinely dead card cannot consume the duty cycle.

- `sampleCount` increments **whether or not the row lands**, so a gap in the
  `sample` column is an exact count of rows lost.
- New last column **`wr_err`** carries the cumulative failure count. A healthy
  run reads 0 throughout.
- Appended at the end of the schema, so parsers reading the first 12 fields are
  unaffected.

New schema:

```
iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6,wr_err
```

This fix is what made the bisection possible. Without it, each configuration
would have produced another silent dead run.

---

## Method

Bisect the chain by substituting a known-good source at successive points, and
read `wr_err` as the instrument.

**Test 1 — logger alone.** E3631A on the 6 V/5 A channel, **5.00 V, 0.5 A
limit**, straight to the Nano's `5V` pin. USB disconnected so there is only one
5 V source. Verify 5.00 V at the terminals before connecting — the Nano has no
regulator, so whatever is on that pin reaches the chips directly.

**Test 2 — add the converter.** E3631A at **3.80 V, 0.5 A limit** into the
MT3608 input, Nano from the MT3608 output. Confirm ~5.03 V at the boost output
before connecting the Nano.

**Test 3 — the deployment configuration.** Battery → MT3608 → Nano, no BMS.

30–45 minutes each is enough: the fault rate was 1.8/min, so a clean half hour
is decisive.

---

## Consequence: deploying without over-discharge protection

The BMS's only field function is over-discharge protection — over-charge is
handled by the bench charger, over-current by the fuse. Removing it means:

- nothing stops the MT3608 pulling the pack toward 2 V as it empties
- below ~2.5 V/cell, copper dissolves from the anode current collector and
  replates as dendrites on the next charge, which can short a cell internally
- **the pack is likely scrap**, and those cells must **never be recharged**
  without checking each one individually — anything under 2.5 V is binned
- **the fuse becomes the only protection** and must be fitted, soldered inline
  rather than relying on spade clips

This is an acceptable documented trade, not a shortcut. It requires retrieval on
schedule.

**Still unexplained:** three different boards from the same panel failed
identically, which a random board fault does not predict. Before writing the BMS
off, check what is printed on one — pads beyond `B+ B− P+ P−` (such as `B1`,
`B2`, `BM`) would mean they are 2S/3S boards that can never turn on at 4.1 V, in
which case the BMS was never faulty, just the wrong part.

---

## Diagnostic signatures worth remembering

| Observation | Means |
|---|---|
| Logging stops, no new `LOGnnnn` file | Nano never rebooted — SD went deaf, not a power loss |
| Logging stops, new file on next boot | genuine power interruption |
| Analog channel pinned ~1017 | signal wire floating / disconnected |
| Analog channel pinned ~10 | sensor unpowered — gate wire broken |
| Gated sensor VCC averages ~0.15 V | healthy (5 V at 3 % duty) |
| Gated sensor VCC reads 0 V | gate wire dead |
| I²C hangs before any output | SDA or SCL held low — usually an unpowered device on the bus |
| `sample` column gap | exact count of rows lost to write failure |

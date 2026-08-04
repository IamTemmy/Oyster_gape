# Nano logger — component-level power analysis

Method: powered from Agilent E3631A (+6V output @ ~5.00 V) into the Nano 5V pin,
USB unplugged (both connected gives a false low/mixed reading — verified: USB+supply
read 15 mA vs 10 mA supply-only). E3631A current readout = primary; handheld DMM in
series = cross-check. Board is an UNALTERED clone Nano (CH340, power LED, regulator
all intact) unless a state says otherwise. Datasheet predictions listed for comparison.

Predictions (5 V, 16 MHz): ATmega328P active ~8-12 mA, deep-sleep ~1 uA (chip);
board parasitics (CH340+LED+reg) ~10-20 mA; DS3231 module ~1-3 mA; SD idle ~few mA;
HAL2425 ~6-10 mA each (to be pinned down by per-sensor delta).

## State 0 — Bare Nano, empty sketch (CPU awake)
Sketch: empty setup()/loop().
E3631A: 21 mA   DMM: 18.3 mA (verify; ~2-3 mA offset expected between instruments)
Note: unaltered board, LED + CH340 intact. Lands in predicted 15-25 mA range.

## State 1 — Bare Nano, deep-sleep sketch (CPU asleep)
Sketch: LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF) in loop.
E3631A: 10 mA, rock steady.
Observation: no visible spike at the 8 s watchdog wake — the wake is only a few ms,
far too brief/rare for the supply's slow display to show. Confirms CPU sleeps ~99.97%
of the time. (Catching the blip would need a scope or fast current profiler.)

### Interpretation (States 0-1)
- CPU active draw = 21 - 10 = ~11 mA  (matches ~8-12 mA datasheet prediction).
- Parasitic floor = 10 mA (CH340 + LED + regulator; do not sleep with the CPU).
- => 10 mA is the FLOOR this board cannot go below without physically stripping
  parasitics. Sets the ceiling on what the strip-down stage can save.

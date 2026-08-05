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

## State 2 — +RTC module (DS3231), asleep and awake
Wiring: SDA=A4 SCL=A5 VCC=5V GND=GND (no SQW needed — not waking from it here).
Asleep: 14 mA (+4 from floor).  Awake: 25 mA (+4).
Observation: DS3231 module LED lit. Chip itself is ~0.1-0.2 mA, so the ~4 mA
delta is mostly the MODULE's power LED + pull-ups -> a strippable parasitic.

## State 3 — +SD module (card inserted), asleep and awake
Wiring: CS=D10 MOSI=D11 MISO=D12 SCK=D13 VCC=5V GND=GND.
Asleep: 16 mA (+2 from State 2).  Awake: 27 mA (+2).
Observation: NO LED on the SD module -> the +2 mA is honest regulator + card
idle, not a vanity LED. SD is one of the efficient parts of the stack.
Note: SD WRITE draws far more (tens of mA in ms bursts) but is invisible to the
E3631A's slow display and averages down over the write duty cycle. The write
spike belongs in the profiler-based battery estimate, not this steady-state set.

## State 4 — +Hall sensors, UNGATED (VDD -> 5V rail, always on), asleep
Baseline (RTC+SD, asleep) = 16 mA. VDD->5V, GND->GND, OUT->A0/A1/A2(/A3).
  +sensor1: 24 mA  (delta 8)
  +sensor2: 31 mA  (delta 7)
  +sensor3: 39 mA  (delta 8)
  +sensor4: 46 mA  (delta 7)  [added as arithmetic check]
=> Per HAL2425 ~7-8 mA (avg ~7.5), tightly consistent, matches ~6-10 mA datasheet.

Six-sensor extrapolation (ungated): 16 + 6*7.5 = ~61 mA asleep.
KEY FINDING: always-on sensors are the LARGEST load in the system (~45 mA for 6),
bigger than the CPU (~11) or all parasitics (~10) combined. This is the #1 target.
Gating sensors (~3 ms on per 100 ms = ~3% duty) drops their averaged draw from
~45 mA to ~1.4 mA. Biggest single win available -> validates GPIO-gating design.

## State 5 — Sensors GATED (VDD -> GPIO D4/D5/D6, direct-GPIO), asleep
Sketch: gated_sensor_test.ino — power sensors ON, settle 3 ms, read, power OFF,
deep-sleep 120 ms. Sensors powered ~3% duty cycle.
Wiring: S1 VDD=D4/OUT=A0, S2 VDD=D5/OUT=A1, S3 VDD=D6/OUT=A2, all GND=GND.

Reading: steady 16 mA, with brief twitches to 20-24 mA.
Interpretation:
  - 16 mA = time-average (sensors mostly OFF/asleep). Matches RTC+SD floor: the
    3 sensors' ~23 mA of always-on draw collapsed to ~0.7 mA averaged.
  - 20-24 mA twitches = the ~3 ms sensor-ON pulses caught on display refresh;
    each ~7-8 mA/sensor. Their presence CONFIRMS the gating is powering sensors
    during reads (flat 16 mA with no twitch would mean sensors never powered).

Comparison (3 sensors, asleep): UNGATED 39 mA -> GATED 16 mA.
  => ~23 mA reclaimed (3 sensors). Six-sensor projection: ~61 mA -> ~17 mA,
     ~44 mA saved by gating alone. Largest single win in the analysis.
Method note: gated draw is a ~3% duty pulse, invisible as a steady value on the
E3631A; the 16 mA average is the battery-relevant number. Exact per-pulse energy
needs a profiler, for the final battery estimate.

Verification: the periodic 20-24 mA twitch is itself proof the gate opens/closes;
a separate "comment out the power-OFF line -> jumps to ~39 mA" test also available.

## State 6 — STRIP #1: remove RTC module power LED (D1), asleep
Before (RTC + LED, sleeping): 14 mA.
Action: removed D1 (green power LED near "POWER" silkscreen) from HW-084 DS3231
module. Left everything else (pull-ups R1-4/P2-4, C1/C2, coin-cell diode, EEPROM,
crystal) intact — one component per measurement.
After (RTC, LED removed, sleeping): 11 mA.
=> LED cost = ~3 mA of pure waste, removed. RTC module now adds only ~1 mA over
   the 10 mA bare-board floor (~= DS3231 chip 0.2 mA + pull-ups). As predicted.
Function check: flashed 01_rtc_test after removal — RTC boots, ticks 1 s/line,
anchors to real time on 's' (time_valid 0->1). No functional damage from strip.

### Sneak-current observations (IGNORE for budget, but instructive)
While probing with the power path half-broken:
  - Nano VCC removed, RTC still wired via I2C: read 3 mA.
  - Only GND to Nano, RTC wired: read 7 mA.
These are NOT any component's real draw. They are phantom/back-feed currents:
with the Nano's proper VCC removed, current sneaks INTO the Nano through the
SDA/SCL signal lines + I2C pull-ups and the chip's input-protection diodes.
Artifact of measuring with a broken power path; vanishes when powered normally.
DEPLOYMENT LESSON: a "powered-off" chip can still be back-fed through its signal
pins -> when we power-gate components, ensure signal lines don't keep them
partially alive (e.g. a gated sensor drawing through its OUT line). "Off" must
be truly off.

Ledger (asleep): floor 10 | +RTC(LED-stripped) 11 | (was +RTC intact 14).

## State 7 — STRIP #2: remove Nano onboard PWR LED, asleep
Before (bare board, sleeping): 10 mA.
Action: removed the "PWR" LED (2nd of 4 in the L/PWR/TX/RX row, above "PWR"
silkscreen). Left L (pin13/SCK), TX, RX untouched — TX/RX only draw during USB,
so they're already dark in deployment.
After (bare board, sleeping): 7 mA.
=> PWR LED cost = ~3 mA (clone-board LED running bright). Board still uploads/runs.

Ledger (bare board, asleep):
  10 mA original floor -> 7 mA after removing PWR LED.
Remaining 7 mA = CH340 USB-serial chip (~5 mA, the big one) + regulator quiescent.
Two LEDs stripped total (RTC ~3mA + Nano PWR ~3mA) = ~6mA of indicator-LED waste
reclaimed. Next target: CH340 (biggest single parasitic, sacrificial strip).

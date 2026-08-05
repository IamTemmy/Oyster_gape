# Nano logger — power analysis summary

Method: Agilent E3631A bench supply @ ~5.00 V into the Nano 5V pin, USB unplugged
(supply's current readout = total draw). DMM used for spot cross-checks. Board is
an unaltered clone Nano unless a row says a component was removed. "Asleep" =
LowPower.powerDown deep sleep.

## Build-up: current vs what's connected (CPU asleep unless noted)

| State | Configuration | Current | Delta (this item) | Notes |
|-------|--------------|--------:|--------------:|-------|
| 0  | Bare Nano, empty sketch (CPU awake)      | 21 mA | —      | unaltered board |
| 1  | Bare Nano, deep-sleep sketch (asleep)    | 10 mA | -11 mA | CPU active ~11 mA; 10 mA = parasitic floor |
| 2  | + RTC module (DS3231)                    | 14 mA | +4 mA  | mostly the module's power LED |
| 3  | + SD module (card in)                    | 16 mA | +2 mA  | no LED on module — efficient |
| 4  | + 3 Hall sensors, UNGATED (VDD->5V)      | 39 mA | +23 mA | ~7.5 mA per sensor (measured over 4) |
| 5  | 3 sensors GATED (VDD->GPIO D4/D5/D6)     | 16 mA | -23 mA | sensors on ~3% duty; biggest single win |

## Strip-down: removing parasitics (bare board, asleep; baseline 10 mA)

| Strip | Action | Before | After | Saved | Result |
|-------|--------|-------:|------:|------:|--------|
| #1 | Remove RTC module power LED (D1)   | 14 mA | 11 mA | ~3 mA | RTC still keeps time |
| #2 | Remove Nano onboard PWR LED        | 10 mA |  7 mA | ~3 mA | board still runs |
| #3 | Cut/lift CH340 VCC (pin 16)        |  7 mA |  7 mA | ~0 mA | no change — chip back-feeds via signal pins |
| #3b| Also lift CH340 TXD+RXD (pins 2,3) |  7 mA |  7 mA | ~0 mA | LEDs off but current unchanged |
| #3c| Remove entire CH340 (other board)  |  7 mA |  7 mA | ~0 mA | confirms CH340 is NOT a meaningful draw here |

## Key findings
- Sensors, always-on, are the LARGEST load (~7.5 mA each; ~45 mA for 6). GPIO-gating
  is the #1 win — cut 3 sensors from +23 mA to ~0.
- Indicator LEDs are pure waste: RTC LED ~3 mA + Nano PWR LED ~3 mA = ~6 mA reclaimed.
- CH340 USB chip draws ~nothing on these clones — removed 3 ways, 0 mA change. It also
  BACK-FEEDS through its signal pins when VCC is cut (2.37 V measured on the lifted VCC
  pin), so it can't be cleanly isolated on a clone. Lesson: "off" isn't off if signal
  lines still feed a chip.
- Remaining ~7 mA floor is NOT the CH340. Leading suspects: AMS1117 regulator quiescent
  draw and/or the ATmega's real deep-sleep current on this clone. Next: measure
  regulator; verify true sleep depth (board warmth / confirm sketch).

## Six-sensor deployment projection (asleep)
- Ungated: 16 (RTC+SD) + 6*7.5 = ~61 mA
- Gated:   ~17 mA  => gating saves ~44 mA on a full node; LED stripping adds ~6 mA.

## Deployment implication
Clean sub-milliamp current can't be reached by stripping a clone Nano (CH340 is
entangled and back-feeds). This is bench evidence FOR building deployment nodes on a
bare ATmega328P (Path B) rather than a modified clone.

---
# Detailed measurement log (chronological notes)

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

## Strip #4 — Remove AMS1117 voltage regulator (asleep)
Before (LEDs stripped, CH340 isolated): 7 mA.
After removing the AMS1117 regulator: 2 mA.
=> Regulator quiescent draw = ~5 mA. THE single biggest remaining parasitic
   (supervisor correctly flagged it). Powered via 5V pin, so the regulator was
   drawing bias/quiescent current with no useful function -> removed.

Bare-board sleeping floor is now ~2 mA (was 10 mA original). This is essentially
the ATmega's real deep-sleep current + residual passives — the practical floor
for this board.

### Updated finding: where the original 10 mA floor actually went
  - CH340 USB chip:      ~0 mA  (surprise — predicted ~5 mA; back-feeds, not a draw)
  - Nano PWR LED:        ~3 mA
  - AMS1117 regulator:   ~5 mA  (the real culprit)
  - ATmega deep sleep + passives: ~2 mA (remaining floor)

### Full-node projection (gated + stripped, asleep)
  stripped bare board ~2 + RTC(LED-stripped) ~1 + SD ~2 + 6 sensors gated ~1.4
  = ~6-7 mA average  (vs 51 mA original unoptimized = ~8x improvement)

This is the number that makes a month+ deployment realistic on a modest battery.

## FINAL — Full stripped + gated node (working, reprogrammable board)
Board: fresh Nano, LEDs + regulator removed, CH340 KEPT (draws ~0, preserves USB).
Built the system up on this working board, measuring each addition (asleep):
  stripped bare board:      2 mA
  + RTC (LED-stripped):     3 mA  (+1)
  + SD (card in):           5 mA  (+2)
  + 3 sensors GATED:        6 mA  (+1 averaged)   <-- FINAL

Gated-sensor reading: settles 6 mA, spikes 11-13 mA on the 3 ms read window
(spikes confirm sensors ARE powered during reads = gating cycling correctly).
First 3 s shows ~19-21 mA = boot delay(3000) awake window; ignored.

Wiring lesson: initial 25 mA reading was a wiring swap — signals were on
D4/D5/D6 and VDD was still on the 5V rail, so sensors ran 24/7. Fixed:
VDD -> D4/D5/D6 (gated power), OUT -> A0/A1/A2 (signal). Then gating worked.

### Full journey
  51 mA  original unaltered board, awake, 3 sensors ungated
   2 mA  stripped bare board (LEDs + regulator removed)
   6 mA  stripped + RTC + SD + 3 sensors gated (working, USB-programmable board)

Six-sensor projection: 5 (RTC+SD) + 6*~0.4 gated ~= 7-8 mA average.
=> ~7-8x better than the 51 mA start, on a board that still uploads over USB.
Makes a month+ deployment realistic on a modest battery.

### Deployment takeaway
Best prototype config = strip LEDs + regulator, KEEP CH340 (0 mA cost, keeps USB),
gate sensors via GPIO (one per pin). For the real build, a bare ATmega328P starts
here without any surgery. Sensor gating + regulator removal are the two big wins;
CH340 removal is pointless (0 mA, and it back-feeds).

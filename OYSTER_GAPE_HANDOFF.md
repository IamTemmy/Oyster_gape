# Oyster_gape — Project Handoff & System Reference

**Purpose of this file:** This is the ground-truth context for continuing the
Oyster_gape project in a *new chat within the project*. Chats in a project can't
see each other, so this document carries everything forward: system design, wiring,
firmware, commands, the Micronas workflow, key findings, the verified results, and
today's plan. Treat it as authoritative. The live code lives in the repo (link
provided separately in the new chat) — read the repo alongside this file.

---

## 1. Project goal

Measure oyster shell **gape** (how far the shell is open, in mm) non-contact, for
biological monitoring (MBRACE). A magnet is mounted on one shell, a Micronas
**HAL 2425** Hall-effect sensor on the other. As the shell opens/closes, the gap
between magnet and sensor changes, and the sensor's output changes with it. The
engineering task has been to **linearize** the sensor so its output reads
proportional to gap distance.

Status: sensor characterization and linearization are **complete** (Stage 02 + 03).
Six magnetic ranges are linearized and verified. Now moving to a **bench simulation**
of the deployed system before water deployment.

---

## 2. Hardware (settled)

- **Sensor:** Micronas HAL 2425 (16 linearization setpoints). Programmed via Micronas
  USB tool on **Sensor 2 / IC2** channel. FW v9.09, SW v3.04.
- **Controller:** ESP32-WROVER (Freenove). NOTE: as of the bench-simulation stage there
  are TWO boards — **Board A** (linearization rig, with the actuator, described here) and
  **Board B** (demo/system board, reads 3 sensors, no actuator). See Section 8.
- **Stepper driver:** Geckodrive G251, fixed 10 microsteps -> 2000 pulses/rev.
  (Board A only — Board B has no stepper/actuator.)
- **Actuator:** leadscrew, long rail (>100 mm travel).
- **Magnet:** 3/8" dia x 1/8" thick, **N40 neodymium, axial**. FIXED — professor
  forbids changing it; all optimization is around this magnet.
- **Leadscrew constant:** 0.001 mm/pulse (1000 pulses/mm; 2.0 mm/rev), verified.

### Pin map
- STEP = GPIO18 -> G251 T10
- DIR  = GPIO19 -> G251 T9
- Sensor analog OUT -> GPIO34 (ADC1_CH6), through the divider below
- Shared GND -> G251 T12
- DIR_AWAY_LEVEL = LOW (positive jog moves carriage AWAY from sensor)
- G251 accepts 3.3 V logic.

### Analog front end (final: ÷1.5 divider, NO cap)
- Two 10 kohm in parallel (= 5 kohm) on the signal side + one 10 kohm to ground;
  GPIO34 reads the node. This is a ÷1.5 divider.
- Sensor max ~3.6 V -> ~2.4 V at pin (no clipping; ESP32 ADC safe).
- No capacitor: adding one reproducibly RAISED noise (~21 mV SD vs ~6 mV without).
  Without cap, ~6 mV SD -> ~0.45 mV after 200-sample averaging. Cap dropped.
- Reads use `analogReadMilliVolts` (factory-calibrated), ADC_11db, 12-bit.

### Output polarity
Output **falls** with gap: shell CLOSED (magnet near) = high (~2830 mV at pin);
shell OPEN (magnet far) = low (~1700 mV baseline). Kept deliberately so the
closed->opening transition sits in the high-resolution region. (Prior team's files
were from the opposite magnet pole, so their curves rose — don't be confused by that.)

---

## 3. Firmware sketches & their commands

All in the repo under `firmware/`. Reminder: **re-upload the sketch after any edit**,
and **close the Arduino Serial Monitor before running the Python logger** (port
conflict). Type serial commands **one at a time** (merged lines get refused).

### `01_leadscrew_calibration/` — DONE (used to verify 0.001 mm/pulse)

### `02_characterization_sweep/`
Sweeps 100 mm @ 0.1 mm steps (1000 pts), 200-sample average + SD per point,
calibrated mV on GPIO34, auto-returns to start.
Commands: `<integer>` jog (pulses), `read`, `sweep`, `pos`, `help`. (No `zero`.)

### `03_linearization/` — the workhorse
Three modes plus jogging. Datum-based (`zero` sets the datum at the block).
Commands:
- `zero` — set datum (carriage resting against block).
- `window <start> <end>` — set the working window in mm (e.g. `window 11.0 24.0`).
- `cal` — visit the 2 calibration endpoints (START then END); press ENTER to advance,
  `q` to abort. Used with Micronas 2-Point Calibration tool.
- `lin` — walk 16 evenly-spaced stops across the window; ENTER advances each. Used
  with Micronas Measurement Tool.
- `verify` — full 100 mm sweep streamed in logger format (the ground-truth check).
- `<integer>` jog, `pos`, `help`.
All modes return to the datum when done.

---

## 4. Host logger & commands

`host/sweep_logger.py` — serial passthrough that replaces CoolTerm. Auto-saves each
sweep to `data/charac_<label>_runN.csv` + `.png` (matplotlib).
Local commands: `:range <label>` (sets the save label), `:port <name>`, `:quit`.
Anything else (including a bare ENTER) is forwarded to the board.

Typical verify flow:
```
:range 12mT_linearized      # in the logger
verify                      # forwarded to the ESP32
```

---

## 5. Micronas programming workflow (per range)

Two Micronas tools used in sequence. **Programmer must be connected to the sensor OUT
line during `cal`/`lin`** (Micronas reads the sensor). **Disconnect the programmer
from OUT during `verify`** (the ESP32 reads the analog output then).

1. **Sensor Settings:** Set Default Values (clears prior linearization — do this each
   range) -> set Magnetic Range -> Write Customer Setup -> Power On Reset.
2. **2-Point Calibration tool:** SCALE_MIN/MAX = 1024 / 29696 (same all ranges),
   OUT_MIN/MAX = **10 / 90** (%FS; keeps inside the full-accuracy band and leaves
   clamp/wire-break headroom). Start Calibration -> run `cal` -> at START click
   "Calibration Point 1", at END click "Calibration Point 2" -> Calculate Signal Path
   -> **Write Signal Path**.
3. **Measurement Tool:** run `lin`. At each of the 16 stops the magnet parks; click
   **Yes** on the tool's "position reached?" prompt (only after the magnet has
   stopped), then press ENTER in the logger to advance. The tool saves the setpoint
   file at the end.
4. **Linearization Tool:** Load Meas. File -> **Linearize** with **Extrapolation
   UNCHECKED** (clamps outside the window) -> **inspect the green Transfer Curve**
   (smooth bow = good; small kink = usually fine; big spike = do NOT write) ->
   **Write Setpoints**.
5. Run `verify` and check straightness.

### CRITICAL rule discovered: start the window PAST the steep shoulder
If the window START sits on the curve's steepest segment, the linearizer's inverse
correction spikes (green Transfer Curve hits hundreds–thousands of %FS) and the write
fails with **"error during programming F:000000"**. Fix: move the window START
further out (further out as the range gets more sensitive). The green Transfer Curve
is the go/no-go; the `verify` sweep is the final arbiter.

---

## 6. Key findings & decisions

- **Per-sensor calibration is mandatory.** Each sensor is characterized AND linearized
  individually (intrinsic spread + mounting/bent-pin variation + magnet pairing).
  **Setpoint files are NOT shareable between sensors.** => 3 sensors = 3 full
  linearizations.
- **Magnetic range acts UPSTREAM of the signal path.** The range dropdown sets
  front-end sensitivity BEFORE the stored calibration. Proven live via **CFX** (the
  raw sensed value in the RAM panel): at a fixed close position, sensitive ranges rail
  CFX at 32767 (full scale) while 100 mT sits at ~16887 (half scale). A railed CFX
  cannot be recovered by any downstream signal-path math.
  - The **signal path** (TCCG/SCALE/OUT/offsets/setpoints) shapes the ramp
    **vertically** (level, slope, straightness). It CANNOT move the ramp along the
    distance axis.
  - The **magnetic range** sets **where** the usable ramp sits on the distance axis
    (via where CFX comes off the rail).
  - CFX is a **live read-only** value (Read RAM); it is not a writable setting.
- **Magnet reach limit ~= 38 mm.** The N40 field flattens by ~38 mm regardless of
  range, so the most sensitive ranges buy almost no extra reach. Largest gapes would
  need a stronger magnet (constrained: magnet is fixed).
- **Fast range-selection diagnostic:** park the magnet at the target's CLOSED-gap
  distance (strongest field) and Read CFX per range. Any range railing at 32767 is
  disqualified without a full sweep — filter losers first, then linearize survivors.

---

## 7. Stage 03 results — the range ladder (6 verified)

Window = verified linear ramp (mm). Linearity = max deviation from a straight line as
% of the full ramp. Slope in mV/mm (at the ÷1.5 pin).

| Range  | Window (mm) | Width | Slope (mV/mm) | Linearity |
|-------:|:-----------:|:-----:|:-------------:|:---------:|
| 3 mT   | 21.8–43.7   | 21.9  | 115           | 3.1%      |
| 6 mT   | 18.2–36.4   | 18.2  | 140           | 2.6%      |
| 12 mT  | 11.4–25.2   | 13.8  | 187           | 2.3%      |
| 25 mT  | 8.5–20.7    | 12.2  | 217           | 2.0%      |
| 50 mT  | 6.4–16.4    | 10.0  | 258           | 1.4% (cleanest) |
| 100 mT | 4.1–14.2    | 10.1  | 260           | 2.6%      |
| 200 mT | —           | —     | —             | IMPRACTICAL (can't get swing + gentle start) |
| 400 mT | —           | —     | —             | IMPRACTICAL (swing too small to calibrate) |

Pattern: more sensitive range -> window moves further out AND gets wider.
Verify CSV/PNG for each range are in `data/` (`charac_<range>_linearized_run1.*`).
Full narrative + the CFX experiment are in `data/analysis/linearization_log.md`.

---

## 8. TODAY'S AGENDA — bench simulation (dry run of deployment)

**Objective:** replicate the deployed system on the bench before water deployment,
using 3 pegs as 3 "oysters," each with magnet on one peg face and sensor on the
opposing face.

### Two-board architecture (DECIDED)
Two separate ESP32 boards, split by role:
- **Board A — Linearization rig** (existing): ESP32 + G251 + leadscrew actuator +
  Micronas USB tool. Used ONLY to characterize / calibrate / linearize / verify a
  sensor. Reads one sensor at a time on GPIO34. Needs the actuator for precise
  positioning.
- **Board B — Demo / system board** (new, separate MCU): ESP32, NO actuator. Reads
  3 already-linearized sensors, converts each to live gape (mm), shows a visual
  readout. This is the deployment-prototype board.

**Flow per sensor: linearize on Board A -> mount on its peg -> wire to Board B.**
Linearization lives in the sensor's own EEPROM, so it travels with the sensor. Board B
never linearizes — it only reads and converts.

### Sensors & range
- 3 pegs, each ~0.5 inch (12.7 mm) closed. Magnet glued to one peg face, sensor glued
  to the opposing face, so magnet-sensor distance ≈ peg gap (verify vs a known gap once
  mounted).
- All 3 sensors linearized at **12 mT** (window 11.4–25.2 mm covers 12.7 mm closed and
  reads up as the peg opens). Same range for all three since the pegs are identical.
- **Per-sensor calibration:** each sensor gets its OWN 12 mT linearization and its OWN
  mV->mm line. Board B stores 3 (slope, intercept) pairs, one per sensor.

### Board B wiring (demo)
- **3 independent ÷1.5 dividers** (same recipe as the rig: 2×10k parallel on signal +
  1×10k to GND), one per sensor, each feeding its own ADC pin. Do NOT share legs.
- ADC pins — **all on ADC1** (see why):
  - Sensor 1 -> GPIO34 (ADC1_CH6, input-only)
  - Sensor 2 -> GPIO35 (ADC1_CH7, input-only)
  - Sensor 3 -> GPIO32 (ADC1_CH4)   [or GPIO39 for an all-input-only trio]
- **Why ADC1 only:** ADC2 shares hardware with the WiFi radio; if the visual readout is
  a WiFi web dashboard, ADC2 reads fail. ADC1 always works.
- **Why input-only pins (34/35/36/39):** no internal pull resistors, so they can't skew
  the ÷1.5 divider ratio. GPIO35 is the cleanest 2nd pin. GPIO32 is a solid 3rd; GPIO39
  is the all-input-only alternative (36/39 can glitch on some silicon — the 200-sample
  averaging handles it).
- Board B has **no STEP/DIR/actuator** — it only reads. Sensors powered from the ESP32
  regulated rail (USB-powered for now).

### New firmware needed (Board B): live 3-sensor gape readout
Read each sensor continuously -> convert mV to mm -> display. Per sensor:
`gape_mm = (mV - b) / m`, with (m, b) from fitting that sensor's own 12 mT verify ramp.
Clamp/flag readings outside 11.4–25.2 mm (shell beyond calibrated window).
**Visual/professional readout:** likely an ESP32 WiFi **web dashboard** with 3 live
gape gauges (this is why ADC1-only matters), or an attached OLED/TFT. Design choice to
finalize in the new chat.

### Phasing (deliberate — don't over-build now)
- **Phase 1 (now):** prove the concept — 3 sensors linearized, wired to Board B, live
  mm readout, visual display. USB-powered, NO battery.
- **Phase 2 (later):** deployment engineering — battery + power management (deep sleep,
  duty-cycling, regulation), waterproofing, on-board logging. Factor into the firmware
  and architecture THEN, not now. (Design Phase 1 so it doesn't block Phase 2.)

### Task order for the new chat
1. **Linearize sensor 2, then sensor 3** on Board A — full Stage 03 loop each at 12 mT,
   step-by-step with each command given explicitly (user wants per-step commands).
2. **Build Board B's 3-sensor live-readout sketch** (+ per-sensor mV->mm conversion).
3. **Build the visual readout** (web dashboard or display).
4. **Physical build:** wire the 3 dividers, glue magnets/sensors to peg faces, mount.
   (User does the physical work once the process is agreed.)
(Battery/power management deferred to Phase 2.)

### Open considerations
- **Mounting vs true distance:** 12.7 mm is the peg gap; confirm the live mm reading
  against a known physical gap. A fresh 2-point cal in the actual peg geometry may be
  worth doing per sensor.
- **Clamp/wire-break band** still 0/100 — set deliberately at deployment (Phase 2).

---

## 9. Command cheat-sheet

**Logger (local):** `:range <label>`, `:port <name>`, `:quit`
**ESP32 `03_linearization`:** `zero`, `window <start> <end>`, `cal`, `lin`, `verify`,
`<integer>` jog, `pos`, `help` (ENTER advances cal/lin; `q` aborts)
**ESP32 `02_characterization`:** `read`, `sweep`, `pos`, `<integer>` jog, `help`

**Micronas quick loop (per range):**
Set Default Values -> Magnetic Range -> Write Customer Setup -> Power On Reset
-> 2-Point Cal (SCALE 1024/29696, OUT 10/90) -> `cal` -> Calculate -> Write Signal Path
-> Measurement Tool -> `lin` (Yes + ENTER x16, saves file)
-> Linearization Tool -> Load -> Linearize (Extrapolation OFF) -> check green curve -> Write Setpoints
-> disconnect programmer from OUT -> `:range <label>` -> `verify`

**Bench reminders:** re-upload sketch after edits; close Serial Monitor before logger;
programmer ON OUT during cal/lin, OFF OUT during verify; one serial command at a time;
present files before download/move instructions.

---

## 10. Repo

- GitHub: `github.com/IamTemmy/Oyster_gape` (working copy at `~/Desktop/Oyster_gape`)
- `firmware/` — the three sketches
- `host/sweep_logger.py` — logger
- `data/` — all characterization + linearized verify CSVs/PNGs
- `data/analysis/` — analysis figures + `linearization_log.md` (full results + CFX experiment)
- `docs/procedures/` — step-by-step procedure docs per stage

---

## 11. Working style (carry this forward)

Researcher-led. Do the data work BEFORE asking questions. Own mistakes explicitly.
One focused question at a time. Phase-by-phase instructions, not everything at once.
Be exact about which interface a command belongs to (TERMINAL vs LOGGER vs
Arduino IDE vs Micronas). The user tunes windows himself from the transfer curve.
Documentation must stay consistent with the existing log style. Point out
design-efficiency improvements when you see them. The `verify` sweep is the
ground-truth arbiter over any tool preview.

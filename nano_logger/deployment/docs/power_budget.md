# Nano logger — power budget & lifetime

Method: powered from Agilent E3631A bench supply (+6V output set to ~5.00 V)
into the Nano 5V pin; supply's own current readout = total system draw.
USB unplugged during measurement (board runs solely on the supply).

## D1 — Baseline (bench logger, no optimization)
Date: 2026-07-31
Setup: 03_nano_logger_bench.ino running; 3 sensors wired + RTC + SD; USB off.
Supply: 4.998 V, current = 0.051 A (51 mA), CV mode.

Interpretation:
  - Board awake 100% of the time (10 Hz loop, never sleeps).
  - Includes parasitic CH340 USB chip + power LED (~15-20 mA, removable on a
    deployment board or by moving to a bare ATmega328P).
  - Sensors powered continuously; SD idling.

Lifetime at 51 mA (constant):
  - 3000 mAh (1x 18650):   ~59 h  (~2.5 days)
  - 10000 mAh pack:        ~196 h (~8 days)
  -> A one-month target is NOT reachable at this draw. This is the number the
     sleep + power-gating stages must beat.

Targets for later stages: D2 sleep between samples, D3 sensor power-gating,
plus stripping parasitic loads. Goal: >= 1 month on a realistic pack.

## D2 (v1, RTC-alarm sleep) — SUPERSEDED
Date: 2026-07-31
Sketch: nano_logger_deploy_D2.ino (RTC DS3231 alarm as wake source, 1 Hz)
Result: sleep-wake mechanism confirmed working (once-per-second wakes; garbled
  serial across sleep = UART powering down = expected). Current dropped from
  51 mA baseline to ~37-40 mA steady with spikes to 51-60 mA on each wake.

Two problems found:
  1. Bug: after waking, the loop took multiple fast samples before sleeping
     again (files showed ~100 ms / ~19 ms row spacing, not clean 1 Hz).
  2. Design limitation: DS3231 alarm min interval = 1 s, forcing 1 Hz. This
     created a false 10Hz-vs-battery tradeoff.

Decision (per supervisor): the RTC alarm is the WRONG waker for pacing. Use the
ATmega's own watchdog/timer interrupt to wake every 100 ms -> keeps 10 Hz AND
sleeps between samples. RTC is retained for TIMESTAMPS only, not pacing. This
dissolves the 1Hz/10Hz tradeoff. Rebuilding D2 on this approach (see D2 v2).

Note: ~40 mA floor here is dominated by always-on parasitics (CH340 USB chip,
power LED, SD idle, RTC) that do NOT sleep — not the CPU. Big wins remain in
stripping those + sensor/SD gating, not in the sleep method itself.

Legacy-repo check: jamesaddy789 repo has NO sleep/timer-interrupt code (verified
by full grep) — it is a millis() busy-loop. The sleep approach is new to our
build, not inherited.

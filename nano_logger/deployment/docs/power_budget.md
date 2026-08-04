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

## D2 v2 — Sleep between samples (timer/watchdog wake, per supervisor)
Date: 2026-07-31
RTC now used for TIMESTAMPS ONLY; pacing done by the ATmega's own timer.
Measured on E3631A @ ~5 V into 5V pin, USB off (same setup as D1 baseline 51 mA).

Option A — D2a_wdt_8hz.ino: watchdog DEEP sleep (powerDown), SLEEP_120MS.
  Measured rate: ~6.6 Hz (120 ms WDT step + read/write overhead).
  Current: steady ~38 mA; spikes 41-60 mA on wake.  (-13 mA vs baseline)

Option B — D2b_timer_10hz.ino: Timer1 CTC 100 ms, light IDLE sleep.
  Measured rate: 9.9 Hz (exact 10 Hz; ms steps cleanly by 100).
  Current: steady ~45 mA; spikes 51-61 mA on wake.  (-6 mA vs baseline)

Finding: deeper sleep (A) saves ~7 mA more than light sleep (B), BUT both are
still ~38-45 mA — i.e. sleep depth is a MINOR lever. The floor is dominated by
always-on PARASITICS that do not sleep with the CPU: CH340 USB-serial chip,
power LED, SD card idle, DS3231. Removing those is the next (big) win.

Leaning: D2b (exact 10 Hz) is the likely deployment choice — keeps the full
spawning-FFT rate and gives up only ~7 mA, which parasitic-stripping (D3) will
dwarf. Decision deferred until D3 numbers are in.

Rate note: in D2a (power-down), millis() does not advance during sleep, so the
ms column is not a clock there — rate judged via RTC iso_time / sample count.
In D2b (IDLE), millis() runs normally and ms steps by ~100 as expected.

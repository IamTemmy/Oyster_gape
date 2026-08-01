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

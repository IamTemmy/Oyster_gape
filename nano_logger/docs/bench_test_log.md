# Nano logger — bench test log

## Step 1 — RTC alone (DS3231), no-coin-cell design
Date: 2026-07-30
Board: known-good Nano
Wiring: SDA=A4, SCL=A5, VCC=5V, GND=GND (HW-084 module, NO backup cell fitted)
Library: RTClib by Adafruit (NOT the similarly-named DS3231 library)
Sketch: sketches/01_rtc_test/01_rtc_test.ino

Design decision: module runs without a backup coin cell (per supervisor —
single main power source per unit). Consequence: RTC loses absolute time on
every power drop and lostPower() is true on each boot. Firmware therefore does
NOT auto-set to compile time (would stamp stale dates after a field reboot).
Instead it reports an "anchored" state (time_valid) and sets time only on an
explicit command. millis() is the always-valid relative axis; absolute time is
anchored explicitly and marked per-row. A coin cell can be added later with no
code change (time_valid would simply start at 1 on boot).

Bug found + fixed: initial sketch printed millis() inside the same snprintf as
other args; on AVR this mangled the value (ms froze ~7-8). Fixed by printing
millis() directly with Serial.println(). ms now increments ~1001/s as expected.

Result: PASS
  - RTC found; time increments 1 s per line.
  - time_valid=1 after set; 's' command works.
  - ms climbs ~1001 ms/line (the ~1 ms/line creep = delay + loop work; this is
    why the logger uses a fixed-cadence scheduler, not plain delay()).
Backup-cell check: N/A (no cell fitted by design).

Status: PASS

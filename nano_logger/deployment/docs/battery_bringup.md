# Battery power-chain bring-up (2026-10-23/24)

First time the logger ran untethered on its own battery pack. Everything
before this session was powered by the E3631A bench supply.

## Chain built
pack -> fuse -> BMS -> MT3608 boost -> decoupling cap -> Nano 5V rail

| Stage | Part | Notes |
|-------|------|-------|
| Pack | 3x Samsung INR18650-35E, PARALLEL (3P) | 4.14 V measured at XT30; ~10,500 mAh |
| Fuse | 1 A Mini/ATM blade, inline waterproof holder | on the + line between XT30 and BMS B+ |
| BMS | FDC-3035 1S, pads B+/B-/P+/P- | battery side on B, load side on P |
| Boost | MT3608 (VIN+/VIN-, VOUT+/VOUT-) | set to 5.03 V before connecting anything |
| Decoupling | 100 uF 50 V electrolytic | across 5V/GND rails, long leg to + |

XT30 kept as a disconnect: pack keeps its connector, the mating half is
soldered to the BMS side. Lets the battery be unplugged without an iron.

## Faults found and fixed

**Dead MT3608 (unit 1).** Chain read 4.14 V at XT30 and at BMS B+/B-, but
collapsed to 1.35 V at BMS P+/P- the moment the converter was attached. The
converter was loading the source down. Swapped for a fresh MT3608 -> 4.14 V
all the way through. Diagnosed by measuring voltage stage by stage along the
chain rather than swapping parts blind.

**MT3608 shipped at 17.4 V output.** These arrive with the trimpot anywhere
in range (up to 28 V). Connecting that to the Nano would have destroyed it.
Adjusted down to 5.03 V with nothing on VOUT, then connected the board.
=> PROCEDURE: always set and verify boost output on an open circuit BEFORE
   connecting any load. This is a hard gate.

**SD module would not initialise.** `FATAL: SD init failed (CS=D10, FAT32
card)` repeating. Ruled out in order: card missing (it wasn't), card bad
(two cards, both later worked), SPI wiring (all four lines continuity-
checked good), SD VCC (5 V present). Swapping the module fixed it.
The original module is the suspect; not yet confirmed bad.

## First battery-powered run - VERIFIED
2,286 rows, 3.81 min, **10.00 Hz, zero overruns, max gap 116 ms**.
Peg presses registered on the three touched channels:
  s1 span 800 counts (~2 episodes), s2 span 652 (~7), s3 span 668 (~7)
  s4/s5/s6 flat (span ~5 counts = noise) - untouched, as expected.
=> Full path proven end to end on battery: pack -> fuse -> BMS -> boost ->
   Nano -> gated sensors -> ADC -> SD.

## RTC anchoring - two gotchas

**1. No coin cell means the anchor dies with power.** `timeAnchored =
!rtc.lostPower()`. Anchoring over USB and then disconnecting power loses it:
the DS3231 stops, lostPower() returns true on the next boot, time_valid=0.
CORRECT SEQUENCE: connect the battery first, then plug USB in for data only,
send 's', then unplug USB. The board resets but the RTC keeps VCC from the
pack, so the anchor survives.

**2. The anchor uses COMPILE time, not wall-clock time.**
`rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))` bakes the build timestamp
into the binary. Pressing 's' sets the clock to whenever the sketch was last
compiled - stale if the firmware has been on the board a while. Error equals
the gap between compile and anchoring.
=> PENDING FIRMWARE FIX: accept a real epoch over serial instead, e.g.
   `if (c=='T') { unsigned long t = Serial.parseInt(); rtc.adjust(DateTime(t));
   timeAnchored = true; }`
   Needed before the field run so 16 nodes carry correct absolute time.

## Weekend soak test
Started 2026-10-24 ~02:50 CDT. Card cleared, RTC anchored (with the stale
compile-time offset noted above), battery connected, USB removed.
On return: pull the card, check 100 ms spacing across the full run, look for
gaps or restarts, confirm all six channels stayed live. Correct absolute time
by comparing the first row's iso_time against the 02:50 CDT start.

## Open items
- [ ] Firmware: replace compile-time anchor with serial epoch input
- [ ] Ask supervisor about adding a CR2032 to the DS3231 (reverses the
      single-power-source decision, but permanently fixes anchor loss)
- [ ] Confirm whether the original SD module and MT3608 #1 are actually dead
- [ ] Weekend log analysis
- [ ] PVC enclosure - blocked until the soak test comes back clean

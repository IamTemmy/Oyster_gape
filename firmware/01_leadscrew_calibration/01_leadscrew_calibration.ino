/* =============================================================================
   01_leadscrew_calibration.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   PURPOSE
     Measure the leadscrew's travel-per-pulse so every later sweep can report
     true millimetres instead of an inherited "magic" constant.

     This sketch does NOT use a stepper library. It toggles the STEP pin
     directly so there is no hidden microstep math: one loop = one pulse.

   HARDWARE (current rig)
     ESP32-WROVER (Freenove)            Geckodrive G251
       GPIO18  --> STEP   ............... Terminal 10 (Step)
       GPIO19  --> DIR    ............... Terminal  9 (Direction)
       GND     --- common ground ........ Terminal 12 (Common / logic GND)
     G251 powered 15-50 VDC, motor phases A//A B//B to the actuator,
     current set by the I-set resistor. The G251 accepts 3.3 V logic, so the
     ESP32 drives STEP/DIR directly (no level shifting).

     The Hall sensor is NOT needed for this step.

   THE KEY FACT
     The G251 is a FIXED 10-microsteps-per-full-step driver. With a 200
     full-step motor, one revolution therefore takes 200 * 10 = 2000 pulses.
     What we cannot know from any datasheet is how far the nut travels per
     revolution -- that is the physical leadscrew lead, which we measure here.

   SERIAL PROTOCOL  (115200 baud, newline line-ending)
     <integer>     Jog that many pulses. Sign = direction.
                   e.g.  20000   -> 10 revolutions in the "away" direction
                         -2000   -> 1 revolution in the "toward" direction
     zero          Reset the pulse counter to 0 at your physical datum.
     mm <value>    Tell it the travel (mm) you measured since the last 'zero';
                   it prints mm/pulse, pulses/mm, and mm/rev.
     pos           Print the current net pulse count.
     help          Reprint this menu.
   =============================================================================
*/

// ----------------------------- Pin assignments -------------------------------
#define STEP_PIN 18
#define DIR_PIN  19

// --------------------------- Mechanical constants ----------------------------
// Used only to report mm/rev. No magic numbers -- all derived and visible.
const int  MOTOR_FULL_STEPS_PER_REV = 200;
const int  G251_MICROSTEPS_PER_STEP = 10;
const long PULSES_PER_REV =
      (long)MOTOR_FULL_STEPS_PER_REV * G251_MICROSTEPS_PER_STEP;   // = 2000

// --------------------------- Direction convention ----------------------------
// Which DIR logic level moves the platform AWAY from the sensor?
// Discover this on the first run: jog a small positive count and watch.
// If positive moves TOWARD the sensor, change HIGH to LOW and re-upload.
const int DIR_AWAY_LEVEL = LOW;

// ----------------------------- Pulse timing ----------------------------------
// Deliberately slow/wide pulses so the motor NEVER misses a step during
// calibration -- a missed step would corrupt mm/pulse. If the motor sounds
// rough or stalls, increase these. The G251 minimum high time is ~1 us.
const unsigned int PULSE_HIGH_US = 300;   // STEP high duration
const unsigned int PULSE_LOW_US  = 300;   // STEP low duration  (~1.6 kHz total)
const unsigned int DIR_SETUP_US  = 50;    // settle time after a DIR change

// ------------------------------- Safety --------------------------------------
// Refuse any single move larger than this so a typo can't run the carriage
// off the end of the screw. Raise it only if your travel really needs it.
const long MAX_PULSES_PER_MOVE = 40000;   // 20 revolutions

// ------------------------------- State ---------------------------------------
long cumulativePulses = 0;   // net pulses since the last 'zero'

// -----------------------------------------------------------------------------
void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN,  DIR_AWAY_LEVEL);

  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(300);
  printBanner();
}

void loop() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();                 // drops spaces and any trailing '\r'
  if (line.length() == 0) return;

  if (line.equalsIgnoreCase("help")) {
    printBanner();
  } else if (line.equalsIgnoreCase("zero")) {
    cumulativePulses = 0;
    Serial.println(F("[ok] Counter zeroed. This is now your datum."));
  } else if (line.equalsIgnoreCase("pos")) {
    printPosition();
  } else if (line.startsWith("mm") || line.startsWith("MM")) {
    handleMeasurement(line.substring(2));
  } else if (isInteger(line)) {
    jog(line.toInt());
  } else {
    Serial.print(F("[?] Unrecognised command: "));
    Serial.println(line);
    Serial.println(F("    Type 'help' for the menu."));
  }
}

// ----------------------------- Core: jog -------------------------------------
void jog(long pulses) {
  if (pulses == 0) {
    Serial.println(F("[?] Zero pulses requested; nothing to do."));
    return;
  }
  if (labs(pulses) > MAX_PULSES_PER_MOVE) {
    Serial.print(F("[!] Refused: move exceeds MAX_PULSES_PER_MOVE ("));
    Serial.print(MAX_PULSES_PER_MOVE);
    Serial.println(F("). Break it into smaller jogs or raise the limit."));
    return;
  }

  bool away = (pulses > 0);
  digitalWrite(DIR_PIN, away ? DIR_AWAY_LEVEL : !DIR_AWAY_LEVEL);
  delayMicroseconds(DIR_SETUP_US);

  long n = labs(pulses);
  Serial.print(F("[move] "));
  Serial.print(n);
  Serial.print(F(" pulses "));
  Serial.print(away ? F("AWAY from sensor") : F("TOWARD sensor"));
  Serial.println(F(" ..."));

  for (long i = 0; i < n; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(PULSE_LOW_US);
  }

  cumulativePulses += pulses;
  Serial.println(F("[done]"));
  printPosition();
}

// ------------------------- Core: compute mm/pulse ----------------------------
void handleMeasurement(String arg) {
  arg.trim();
  if (arg.length() == 0) {
    Serial.println(F("[?] Usage: mm <measured travel in mm>, e.g.  mm 8.04"));
    return;
  }
  float travel = arg.toFloat();
  long  p = labs(cumulativePulses);

  if (travel <= 0.0) {
    Serial.println(F("[?] Travel must be a positive number of millimetres."));
    return;
  }
  if (p == 0) {
    Serial.println(F("[!] Counter is at 0. Did you 'zero' then jog before measuring?"));
    return;
  }

  float mmPerPulse  = travel / (float)p;
  float pulsesPerMm = (float)p / travel;
  float mmPerRev    = mmPerPulse * (float)PULSES_PER_REV;

  Serial.println(F("---------------- CALIBRATION RESULT ----------------"));
  Serial.print(F("  pulses moved : ")); Serial.println(p);
  Serial.print(F("  travel (mm)  : ")); Serial.println(travel, 3);
  Serial.print(F("  mm / pulse   : ")); Serial.println(mmPerPulse, 6);
  Serial.print(F("  pulses / mm  : ")); Serial.println(pulsesPerMm, 3);
  Serial.print(F("  mm / rev     : ")); Serial.println(mmPerRev, 4);
  Serial.println(F("  (repeat 3+ times, both directions, and average)"));
  Serial.println(F("----------------------------------------------------"));
}

// ------------------------------- Helpers -------------------------------------
void printPosition() {
  Serial.print(F("[pos] net pulses since datum: "));
  Serial.print(cumulativePulses);
  Serial.print(F("  ("));
  Serial.print((float)cumulativePulses / (float)PULSES_PER_REV, 3);
  Serial.println(F(" rev)"));
}

bool isInteger(const String &s) {
  if (s.length() == 0) return false;
  int start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (start == s.length()) return false;            // just a sign
  for (int i = start; i < s.length(); i++) {
    if (!isDigit(s[i])) return false;
  }
  return true;
}

void printBanner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : leadscrew mm-per-pulse calibration ==="));
  Serial.print  (F("    G251 fixed 10x microstepping -> "));
  Serial.print  (PULSES_PER_REV);
  Serial.println(F(" pulses per revolution"));
  Serial.println(F("    <integer>   jog N pulses (sign = direction)"));
  Serial.println(F("    zero        set counter to 0 at your datum"));
  Serial.println(F("    mm <value>  enter measured travel -> prints mm/pulse"));
  Serial.println(F("    pos         show net pulse count"));
  Serial.println(F("    help        show this menu"));
  Serial.println(F("========================================================"));
}

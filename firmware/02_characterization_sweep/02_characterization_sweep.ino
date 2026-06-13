/* =============================================================================
   02_characterization_sweep.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   PURPOSE
     Move the magnet away from the sensor in fixed distance steps and, at each
     position, take a heavily-averaged ADC reading. Prints a CSV of
     distance vs sensor output so we can see the magnet's real response curve
     and choose the magnetic range (25 mT vs 50 mT).

   WHAT'S NEW vs the inherited code
     - Reads on GPIO34 (ADC1), not GPIO4 (ADC2).
     - Uses analogReadMilliVolts(), which applies the ESP32's factory ADC
       calibration -> a more linear measurement, so the ADC's own nonlinearity
       is not baked into the curve we later invert.
     - Averages 200 samples per point and reports the standard deviation, so we
       can SEE how noisy the signal is and whether the averaging is enough.
     - Distance is real millimetres, using the measured 0.001 mm/pulse constant.

   HARDWARE
       GPIO18 -> STEP  -> G251 T10        GPIO19 -> DIR -> G251 T9
       GPIO34 <- sensor analog (via 2x10k divider, 0.1uF cap optional)
       GND shared with G251 T12
     Sensor powered from the board's 5 V (ratiometric) with the magnet on the
     carriage. The Micronas programmer is NOT needed during a sweep.

   SERIAL PROTOCOL (115200 baud, Newline line-ending)
     <integer>   Jog N pulses (sign = direction) to position the carriage.
                 positive = AWAY from sensor   (1000 pulses = 1 mm)
     read        Take one averaged reading here (wiring/level sanity check).
     sweep       Run the full characterization sweep from the current position,
                 then return to where it started.
     pos         Show net pulse count / distance from datum.
     help        Reprint the menu.

   CALIBRATION CONSTANT comes from Procedure 01.
   =============================================================================
*/

// ----------------------------- Pin assignments -------------------------------
#define STEP_PIN   18
#define DIR_PIN    19
#define SENSOR_PIN 34            // ADC1_CH6, input-only

// --------------------------- Measured constant (Stage 01) --------------------
const float MM_PER_PULSE  = 0.001;     // 1000 pulses per mm, 2.0 mm/rev
const long  PULSES_PER_MM = 1000;

// --------------------------- Direction convention ----------------------------
const int DIR_AWAY_LEVEL = LOW;        // positive jog moves AWAY from sensor

// ------------------------------ Sweep settings -------------------------------
// Keep SWEEP_DISTANCE_MM inside your clear travel and leave margin before the
// far end. Change this one number to adjust the span.
const float SWEEP_DISTANCE_MM = 100.0;  // total distance to sweep
const float STEP_MM           = 0.1;    // 0.1 mm -> 1000 points over 100 mm

// ------------------------------ Sampling -------------------------------------
const int          AVG_SAMPLES     = 200;   // reads averaged per point
const unsigned int SAMPLE_DELAY_US = 500;   // gap between samples
const unsigned int SETTLE_MS       = 100;   // wait after a move before sampling

// ----------------------------- Pulse timing ----------------------------------
const unsigned int PULSE_HIGH_US = 300;
const unsigned int PULSE_LOW_US  = 300;
const unsigned int DIR_SETUP_US  = 50;

// ------------------------------- Safety --------------------------------------
const long MAX_PULSES_PER_MOVE = 40000;     // manual-jog guard (20 rev)

// ------------------------------- State ---------------------------------------
long cumulativePulses = 0;

// -----------------------------------------------------------------------------
void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN,  DIR_AWAY_LEVEL);

  analogReadResolution(12);
  // ~0-3.1 V input range; our divider keeps the signal under ~2.4 V.
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);

  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(300);
  printBanner();
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line.equalsIgnoreCase("help"))       printBanner();
  else if (line.equalsIgnoreCase("read"))  singleRead();
  else if (line.equalsIgnoreCase("sweep")) runSweep();
  else if (line.equalsIgnoreCase("pos"))   printPosition();
  else if (isInteger(line))                jogVerbose(line.toInt());
  else {
    Serial.print(F("[?] Unrecognised: ")); Serial.println(line);
    Serial.println(F("    Type 'help'."));
  }
}

// ------------------------- Sampling with statistics --------------------------
void sampleStats(float &avg_mV, float &sd_mV) {
  double sum = 0, sumsq = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    uint32_t mv = analogReadMilliVolts(SENSOR_PIN);
    sum   += mv;
    sumsq += (double)mv * mv;
    delayMicroseconds(SAMPLE_DELAY_US);
  }
  double mean = sum / AVG_SAMPLES;
  double var  = sumsq / AVG_SAMPLES - mean * mean;
  if (var < 0) var = 0;
  avg_mV = mean;
  sd_mV  = sqrt(var);
}

void singleRead() {
  float avg, sd;
  sampleStats(avg, sd);
  Serial.print(F("[read] "));
  Serial.print(avg, 1); Serial.print(F(" mV   (sd "));
  Serial.print(sd, 2);  Serial.println(F(" mV)"));
}

// ------------------------------ The sweep ------------------------------------
void runSweep() {
  long startPulses = cumulativePulses;
  long stepPulses  = lround(STEP_MM * PULSES_PER_MM);          // 100
  int  numSteps    = lround(SWEEP_DISTANCE_MM / STEP_MM);      // 90

  Serial.println(F("# --- characterization sweep ---"));
  Serial.print(F("# step_mm="));   Serial.print(STEP_MM, 3);
  Serial.print(F("  points="));    Serial.print(numSteps + 1);
  Serial.print(F("  avg_samples=")); Serial.print(AVG_SAMPLES);
  Serial.print(F("  settle_ms="));   Serial.println(SETTLE_MS);
  Serial.println(F("Distance_mm,Reading_mV,SD_mV"));

  for (int i = 0; i <= numSteps; i++) {
    if (i > 0) pulses(stepPulses);     // step one increment away
    delay(SETTLE_MS);
    float avg, sd;
    sampleStats(avg, sd);
    Serial.print(i * STEP_MM, 3); Serial.print(',');
    Serial.print((long)lround(avg)); Serial.print(',');
    Serial.println(sd, 2);
  }

  Serial.println(F("# Sweep complete"));
  pulses(startPulses - cumulativePulses);   // return to start
  Serial.println(F("# Returned to start position"));
}

// ------------------------- Motion primitives ---------------------------------
void pulses(long p) {                 // quiet move, updates counter
  if (p == 0) return;
  bool away = (p > 0);
  digitalWrite(DIR_PIN, away ? DIR_AWAY_LEVEL : !DIR_AWAY_LEVEL);
  delayMicroseconds(DIR_SETUP_US);
  long n = labs(p);
  for (long i = 0; i < n; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(PULSE_LOW_US);
  }
  cumulativePulses += p;
}

void jogVerbose(long p) {
  if (labs(p) > MAX_PULSES_PER_MOVE) {
    Serial.print(F("[!] Refused: exceeds MAX_PULSES_PER_MOVE ("));
    Serial.print(MAX_PULSES_PER_MOVE); Serial.println(F(")."));
    return;
  }
  pulses(p);
  printPosition();
}

// ------------------------------- Helpers -------------------------------------
void printPosition() {
  Serial.print(F("[pos] "));
  Serial.print((float)cumulativePulses * MM_PER_PULSE, 3);
  Serial.print(F(" mm from datum ("));
  Serial.print(cumulativePulses);
  Serial.println(F(" pulses)"));
}

bool isInteger(const String &s) {
  if (s.length() == 0) return false;
  int start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (start == s.length()) return false;
  for (int i = start; i < s.length(); i++)
    if (!isDigit(s[i])) return false;
  return true;
}

void printBanner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : characterization sweep ==="));
  Serial.print  (F("    sweep "));   Serial.print(SWEEP_DISTANCE_MM, 1);
  Serial.print  (F(" mm in "));      Serial.print(STEP_MM, 2);
  Serial.println(F(" mm steps, away from the sensor"));
  Serial.println(F("    <integer>  jog N pulses (1000 = 1 mm, + = away)"));
  Serial.println(F("    read       one averaged reading here"));
  Serial.println(F("    sweep      run the sweep, then return to start"));
  Serial.println(F("    pos        show position"));
  Serial.println(F("    help       this menu"));
  Serial.println(F("============================================"));
}

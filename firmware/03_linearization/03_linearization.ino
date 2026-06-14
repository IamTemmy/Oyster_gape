/* =============================================================================
   03_linearization.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   PURPOSE
     Positioning helper for linearizing the HAL 2425, one magnetic range at a
     time. Same program for every range — you just feed it that range's window.

     Three modes:
       cal     -> visits the 2 calibration endpoints (START, END), waits for you
                  to press ENTER at each while you click Calibration Point 1 / 2
                  in the Micronas 2-Point Calibration tool.
       lin     -> walks 16 evenly-spaced stops across START..END, waiting for
                  ENTER at each while you register the reading in the Micronas
                  Measurement Tool. (16 = the HAL 2425's setpoint count.)
       verify  -> full 100 mm sweep of the now-linearized sensor, streamed in the
                  same format the host logger captures, so you can see the
                  straightened curve (flat clamp -> straight ramp -> flat clamp).

     Nothing here moves on a timer: cal and lin WAIT for your keypress at every
     stop, so you can never miss a point.

   HARDWARE  (unchanged from Stage 02)
       GPIO18 -> STEP -> G251 T10     GPIO19 -> DIR -> G251 T9
       GPIO34 <- sensor analog (÷1.5 divider)     GND shared
     Connection note (see procedure 03):
       - cal / lin : the Micronas programmer reads the sensor -> programmer ON.
       - verify    : the ESP32 reads the sensor -> programmer OFF the OUT line.

   SERIAL (115200, Newline), one command at a time:
       zero              set the current position as the datum (rest on block first)
       window <a> <b>    set this range's window, e.g.  window 1.0 10.0
       cal               run the 2-point calibration positioning
       lin               run the 16-stop linearization walk
       verify            full 100 mm sweep of the linearized output
       <integer>         jog N pulses (1000 = 1 mm, + = away) to position
       pos               show position and current window
       help              menu
     During cal / lin: press ENTER to advance, or type  q  to abort.

   Distance constant from Procedure 01: 0.001 mm/pulse.
   =============================================================================
*/

#define STEP_PIN   18
#define DIR_PIN    19
#define SENSOR_PIN 34

const float MM_PER_PULSE  = 0.001;
const long  PULSES_PER_MM = 1000;
const int   DIR_AWAY_LEVEL = LOW;

// ---- linearization ----
const int N_LIN_POINTS = 16;            // HAL 2425 setpoint count

// ---- verify sweep (matches Stage 02 so the logger captures it) ----
const float SWEEP_DISTANCE_MM = 100.0;
const float STEP_MM           = 0.1;
const int          AVG_SAMPLES     = 200;
const unsigned int SAMPLE_DELAY_US = 500;
const unsigned int SETTLE_MS       = 100;

// ---- pulse timing ----
const unsigned int PULSE_HIGH_US = 300;
const unsigned int PULSE_LOW_US  = 300;
const unsigned int DIR_SETUP_US  = 50;
const long MAX_PULSES_PER_MOVE = 40000;     // manual-jog guard only

// ---- state ----
long  cumulativePulses = 0;
float winStart = 1.0;                       // default = 400 mT window
float winEnd   = 10.0;

void setup() {
  pinMode(STEP_PIN, OUTPUT); pinMode(DIR_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW); digitalWrite(DIR_PIN, DIR_AWAY_LEVEL);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
  Serial.begin(115200); Serial.setTimeout(50);
  delay(300); banner();
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (line.length() == 0) return;

  if      (line.equalsIgnoreCase("help"))   banner();
  else if (line.equalsIgnoreCase("pos"))    showPos();
  else if (line.equalsIgnoreCase("zero"))   { cumulativePulses = 0; Serial.println(F("[zero] datum set here")); }
  else if (line.equalsIgnoreCase("cal"))    doCal();
  else if (line.equalsIgnoreCase("lin"))    doLin();
  else if (line.equalsIgnoreCase("verify")) doVerify();
  else if (line.startsWith("window"))       setWindow(line);
  else if (isInteger(line))                 jog(line.toInt());
  else { Serial.print(F("[?] ")); Serial.println(line); Serial.println(F("    type 'help'")); }
}

// ----------------------------- modes ----------------------------------------
void doCal() {
  Serial.println(F("# CALIBRATION positioning (2 points)"));
  moveTo(mmToPulses(winStart));
  Serial.print(F(">> At START = ")); Serial.print(winStart,2);
  Serial.println(F(" mm.  Click 'Calibration Point 1' in Micronas, then press ENTER (q=abort)."));
  if (!waitAdvance()) { returnDatum(); return; }
  moveTo(mmToPulses(winEnd));
  Serial.print(F(">> At END   = ")); Serial.print(winEnd,2);
  Serial.println(F(" mm.  Click 'Calibration Point 2' in Micronas, then press ENTER."));
  if (!waitAdvance()) { returnDatum(); return; }
  returnDatum();
  Serial.println(F("# Done. Now 'Calculate Signal Path' + 'Write Signal Path' in Micronas."));
}

void doLin() {
  Serial.println(F("# LINEARIZATION walk (16 stops)"));
  long sP = mmToPulses(winStart), eP = mmToPulses(winEnd);
  for (int i = 0; i < N_LIN_POINTS; i++) {
    float frac = (N_LIN_POINTS == 1) ? 0.0 : (float)i / (N_LIN_POINTS - 1);
    long tp = lround(sP + frac * (eP - sP));
    moveTo(tp);
    Serial.print(F(">> Stop ")); Serial.print(i + 1); Serial.print(F("/")); Serial.print(N_LIN_POINTS);
    Serial.print(F("  dist = ")); Serial.print(tp * MM_PER_PULSE, 2);
    Serial.println(F(" mm.  Register this point in the Measurement Tool, then press ENTER."));
    if (!waitAdvance()) { returnDatum(); return; }
  }
  returnDatum();
  Serial.println(F("# Done. Save the file in Micronas, then 'Write Setpoints'."));
}

void doVerify() {
  moveTo(0);                                   // start from datum (block)
  Serial.println(F("# --- characterization sweep ---"));
  Serial.print(F("# verify(linearized) step_mm=")); Serial.print(STEP_MM, 3);
  Serial.print(F(" window=")); Serial.print(winStart,1); Serial.print(F("-")); Serial.println(winEnd,1);
  Serial.println(F("Distance_mm,Reading_mV,SD_mV"));
  long stepPulses = lround(STEP_MM * PULSES_PER_MM);
  int  numSteps   = lround(SWEEP_DISTANCE_MM / STEP_MM);
  for (int i = 0; i <= numSteps; i++) {
    if (i > 0) pulses(stepPulses);
    delay(SETTLE_MS);
    float avg, sd; sampleStats(avg, sd);
    Serial.print(i * STEP_MM, 3); Serial.print(',');
    Serial.print((long)lround(avg)); Serial.print(',');
    Serial.println(sd, 2);
  }
  Serial.println(F("# Sweep complete"));
  returnDatum();
  Serial.println(F("# Returned to start position"));
}

// --------------------------- helpers ----------------------------------------
bool waitAdvance() {
  while (!Serial.available()) delay(5);
  String s = Serial.readStringUntil('\n'); s.trim();
  if (s.equalsIgnoreCase("q") || s.equalsIgnoreCase("abort")) {
    Serial.println(F("[aborted]")); return false;
  }
  return true;
}

void sampleStats(float &avg_mV, float &sd_mV) {
  double sum = 0, sumsq = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    uint32_t mv = analogReadMilliVolts(SENSOR_PIN);
    sum += mv; sumsq += (double)mv * mv;
    delayMicroseconds(SAMPLE_DELAY_US);
  }
  double mean = sum / AVG_SAMPLES, var = sumsq / AVG_SAMPLES - mean * mean;
  if (var < 0) var = 0;
  avg_mV = mean; sd_mV = sqrt(var);
}

long mmToPulses(float mm) { return lround(mm * PULSES_PER_MM); }
void moveTo(long target)  { pulses(target - cumulativePulses); }
void returnDatum()        { moveTo(0); }

void pulses(long p) {
  if (p == 0) return;
  digitalWrite(DIR_PIN, (p > 0) ? DIR_AWAY_LEVEL : !DIR_AWAY_LEVEL);
  delayMicroseconds(DIR_SETUP_US);
  long n = labs(p);
  for (long i = 0; i < n; i++) {
    digitalWrite(STEP_PIN, HIGH); delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(STEP_PIN, LOW);  delayMicroseconds(PULSE_LOW_US);
  }
  cumulativePulses += p;
}

void jog(long p) {
  if (labs(p) > MAX_PULSES_PER_MOVE) {
    Serial.print(F("[!] Refused: exceeds ")); Serial.println(MAX_PULSES_PER_MOVE); return;
  }
  pulses(p); showPos();
}

void setWindow(String line) {
  line.remove(0, 6); line.trim();              // drop "window"
  int sp = line.indexOf(' ');
  if (sp < 0) { Serial.println(F("[?] usage: window <start_mm> <end_mm>")); return; }
  winStart = line.substring(0, sp).toFloat();
  winEnd   = line.substring(sp + 1).toFloat();
  Serial.print(F("[window] ")); Serial.print(winStart, 2);
  Serial.print(F(" -> ")); Serial.print(winEnd, 2); Serial.println(F(" mm"));
}

void showPos() {
  Serial.print(F("[pos] ")); Serial.print(cumulativePulses * MM_PER_PULSE, 3);
  Serial.print(F(" mm   window ")); Serial.print(winStart, 2);
  Serial.print(F("-")); Serial.print(winEnd, 2); Serial.println(F(" mm"));
}

bool isInteger(const String &s) {
  int start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (start == (int)s.length()) return false;
  for (int i = start; i < (int)s.length(); i++) if (!isDigit(s[i])) return false;
  return true;
}

void banner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : linearization positioning ==="));
  Serial.println(F("  zero            set datum (rest on block first)"));
  Serial.println(F("  window <a> <b>  set this range's window (mm)"));
  Serial.println(F("  cal             2-point calibration positioning"));
  Serial.println(F("  lin             16-stop linearization walk"));
  Serial.println(F("  verify          100 mm sweep of linearized output"));
  Serial.println(F("  <integer>       jog pulses (1000 = 1 mm, + = away)"));
  Serial.println(F("  pos / help"));
  Serial.println(F("  (during cal/lin: ENTER = advance, q = abort)"));
  Serial.println(F("==============================================="));
}

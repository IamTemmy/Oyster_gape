/* =============================================================================
   04_board_b_readout.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   ⚠  SUPERSEDED — REFERENCE ONLY (12 mT stage).
      This sketch carries the OLD 12 mT calibration constants. The peg sensors are
      now burned at 25 mT, so flashing this would report WRONG millimetres (25 mT
      voltages read through 12 mT lines — no error, just silently wrong).
      >>> Flash firmware/06_board_b_tare for correct, tared gape. <<<
      Kept as the minimal serial-only readout reference from the 12 mT stage.
   -----------------------------------------------------------------------------
   PURPOSE
     Board B — the demo / system board. NO actuator, NO Micronas tool. It reads
     the THREE already-linearized peg sensors continuously and converts each to
     live gape (mm) using that sensor's own mV->mm line (from Board A's 12 mT
     verify ramp; see data/analysis/deployment_calibration.md).

       gape_mm = (mV - b) / m        (m = slope mV/mm, b = intercept mV)

     Each sensor keeps its OWN (m, b) and its OWN valid ramp. Readings outside a
     sensor's ramp are FLAGGED (not silently trusted): LO/HI = shell past the
     calibrated window; CLAMP = output pinned at the wire-break/clamp band
     (sensor railed or line disconnected).

     This is Phase 1: prove the concept over USB serial. The visual web
     dashboard (Phase 1b) will reuse the exact conversion core below. Battery /
     power management is Phase 2 — deliberately NOT built in here.

   HARDWARE  (Board B — demo board, no stepper)
     Three INDEPENDENT ÷1.5 dividers (2×10k parallel on signal + 1×10k to GND),
     one per sensor, each feeding its own ADC1 pin. Do NOT share divider legs.
       S1 -> GPIO34  (ADC1_CH6, input-only)
       S2 -> GPIO35  (ADC1_CH7, input-only)
       S3 -> GPIO32  (ADC1_CH4)
     ALL on ADC1 on purpose: ADC2 shares hardware with the WiFi radio, so ADC2
     reads fail once the web dashboard is up. ADC1 always works. Sensors powered
     from the ESP32 regulated rail (USB-powered for now).

   SERIAL (115200, Newline), one command at a time:
       read            one-shot verbose read of all 3 sensors (mV, ±SD, mm, flag)
       stream on/off   start/stop the continuous live line (default: on)
       rate <ms>       set the refresh period in ms for the live line
       lines           reprint the 3 stored mV->mm conversion lines
       help            menu

   Read routine mirrors the rig (analogReadMilliVolts, ADC_11db, 12-bit). NOTE:
   averaging only shrinks JITTER, not the mean, so it does NOT shift the (m, b)
   calibration — we simply use fewer samples here for a smoother live refresh.
   =============================================================================
*/

// ---- per-sensor calibration (from data/analysis/deployment_calibration.md) ----
struct Sensor {
  const char* name;
  uint8_t pin;
  float m;         // slope, mV/mm
  float b;         // intercept, mV  (negative)
  float rampLo;    // valid ramp low, mm
  float rampHi;    // valid ramp high, mm
  float clampLo;   // low clamp / wire-break band, mV
  float clampHi;   // high clamp band, mV
};

Sensor SENSORS[] = {
  //  name  pin     m       b     rampLo rampHi clampLo clampHi
  {  "S1",  34,  199.8f, -1810.0f, 11.4f, 24.4f,  299.0f, 3150.0f },
  {  "S2",  35,  197.8f, -1799.0f, 11.5f, 24.6f,  317.0f, 3150.0f },
  {  "S3",  32,  200.6f, -1863.0f, 11.7f, 24.5f,  316.0f, 3150.0f },
};
const int N_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

// ---- read routine (same method as the rig; lighter averaging for live speed) ----
const int          AVG_SAMPLES     = 100;   // 100 -> jitter ~0.6mV/sqrt(100)≈0.06mV
const unsigned int SAMPLE_DELAY_US = 250;
const float        CLAMP_MARGIN_MV = 20.0;  // how close to a clamp counts as "pinned"

// ---- live stream ----
bool         streaming   = true;
unsigned int refreshMs    = 250;            // ~4 Hz; read time (~75ms) dominates
unsigned long lastStream  = 0;

void setup() {
  analogReadResolution(12);
  for (int i = 0; i < N_SENSORS; i++)
    analogSetPinAttenuation(SENSORS[i].pin, ADC_11db);
  Serial.begin(115200); Serial.setTimeout(50);
  delay(300); banner(); printLines();
}

void loop() {
  // --- serial commands (non-blocking) ---
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if (line.length() > 0) handle(line);
  }
  // --- live line ---
  if (streaming && (millis() - lastStream >= refreshMs)) {
    lastStream = millis();
    streamLine();
  }
}

// ------------------------------ commands ------------------------------------
void handle(String line) {
  if      (line.equalsIgnoreCase("help"))  { banner(); }
  else if (line.equalsIgnoreCase("lines")) { printLines(); }
  else if (line.equalsIgnoreCase("read"))  { readAllVerbose(); }
  else if (line.equalsIgnoreCase("stream on"))  { streaming = true;  Serial.println(F("[stream] on")); }
  else if (line.equalsIgnoreCase("stream off")) { streaming = false; Serial.println(F("[stream] off")); }
  else if (line.startsWith("rate")) {
    int v = line.substring(4).toInt();
    if (v >= 20) { refreshMs = v; Serial.print(F("[rate] ")); Serial.print(refreshMs); Serial.println(F(" ms")); }
    else Serial.println(F("[?] usage: rate <ms>   (min 20)"));
  }
  else { Serial.print(F("[?] ")); Serial.println(line); Serial.println(F("    type 'help'")); }
}

// ------------------------------ reads ---------------------------------------
// Average N calibrated-mV samples on one pin; also return SD (noise only).
void sampleStats(uint8_t pin, float &avg_mV, float &sd_mV) {
  double sum = 0, sumsq = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    uint32_t mv = analogReadMilliVolts(pin);
    sum += mv; sumsq += (double)mv * mv;
    delayMicroseconds(SAMPLE_DELAY_US);
  }
  double mean = sum / AVG_SAMPLES, var = sumsq / AVG_SAMPLES - mean * mean;
  if (var < 0) var = 0;
  avg_mV = mean; sd_mV = sqrt(var);
}

float mmFromMv(const Sensor &s, float mv) { return (mv - s.b) / s.m; }

// Flag priority: hardware clamp first, then out-of-window, else OK.
const char* flagFor(const Sensor &s, float mv, float mm) {
  if (mv <= s.clampLo + CLAMP_MARGIN_MV) return "CLAMP-LO";
  if (mv >= s.clampHi - CLAMP_MARGIN_MV) return "CLAMP-HI";
  if (mm <  s.rampLo)                    return "LO";     // more closed than window
  if (mm >  s.rampHi)                    return "HI";     // open past window
  return "OK";
}

// Compact one-liner for the live stream.
void streamLine() {
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    float mm = mmFromMv(SENSORS[i], mv);
    const char* f = flagFor(SENSORS[i], mv, mm);
    if (i) Serial.print(F(" | "));
    Serial.print(SENSORS[i].name); Serial.print(' ');
    Serial.print(mm, 2); Serial.print(F(" mm "));
    Serial.print(f);
    Serial.print(F(" (")); Serial.print((long)lround(mv)); Serial.print(F(" mV)"));
  }
  Serial.println();
}

// Verbose one-shot for all sensors.
void readAllVerbose() {
  Serial.println(F("# one-shot read"));
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    float mm = mmFromMv(SENSORS[i], mv);
    const char* f = flagFor(SENSORS[i], mv, mm);
    Serial.print(F("  ")); Serial.print(SENSORS[i].name);
    Serial.print(F(" (GPIO")); Serial.print(SENSORS[i].pin); Serial.print(F(")  "));
    Serial.print((long)lround(mv)); Serial.print(F(" mV  ±"));
    Serial.print(sd, 1); Serial.print(F("   ->  "));
    Serial.print(mm, 2); Serial.print(F(" mm   ["));
    Serial.print(f); Serial.println(F("]"));
  }
}

// ------------------------------ banners -------------------------------------
void printLines() {
  Serial.println(F("# conversion lines (mV->mm), per sensor:"));
  for (int i = 0; i < N_SENSORS; i++) {
    Serial.print(F("#   ")); Serial.print(SENSORS[i].name);
    Serial.print(F(" GPIO")); Serial.print(SENSORS[i].pin);
    Serial.print(F(":  mm = (mV - (")); Serial.print(SENSORS[i].b, 1);
    Serial.print(F(")) / ")); Serial.print(SENSORS[i].m, 1);
    Serial.print(F("   ramp ")); Serial.print(SENSORS[i].rampLo, 1);
    Serial.print(F("-")); Serial.print(SENSORS[i].rampHi, 1); Serial.println(F(" mm"));
  }
}

void banner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : Board B — 3-sensor live gape readout ==="));
  Serial.println(F("  read            one-shot verbose read of all 3 sensors"));
  Serial.println(F("  stream on/off   continuous live line (default on)"));
  Serial.println(F("  rate <ms>       live refresh period (min 20)"));
  Serial.println(F("  lines           reprint the 3 mV->mm conversion lines"));
  Serial.println(F("  help            this menu"));
  Serial.println(F("  S1->GPIO34  S2->GPIO35  S3->GPIO32  (all ADC1, ÷1.5)"));
  Serial.println(F("=========================================================="));
}

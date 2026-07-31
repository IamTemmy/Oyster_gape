/*
 * sensor_read_test.ino  —  Oyster_gape / nano_logger (bench, sensors only, step 3a)
 * Reads 3 Hall sensors on A0/A1/A2 with the settling double-read, prints raw
 * 10-bit values plus a rolling min/max/spread so the noise floor is visible.
 * No RTC, no SD. 5 V native, no divider.
 *
 * SAMPLE-RATE DECISION: delay is 1000 ms (1 Hz) ON PURPOSE for this test.
 * This is a human eyeball test — 1 Hz is slow enough to read resting values
 * and watch a channel respond when a peg moves. The DEPLOYMENT logger runs at
 * 10 Hz (100 ms) to match the spawning-FFT band; that rate is set in the
 * logger sketch, not here. Print rate does NOT affect the noise-floor (spread)
 * measurement, which reflects real ADC jitter regardless of print cadence.
 */
const uint8_t PINS[3] = { A0, A1, A2 };
int   mn[3], mx[3];
bool  first = true;
unsigned long lastReset = 0;

int readSensor(uint8_t pin) {
  analogRead(pin);          // throwaway: let ADC settle
  return analogRead(pin);   // real reading
}

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 3000) {}
  Serial.println(F("Sensor read test — A0 A1 A2 (raw 0..1023)"));
  Serial.println(F("Columns: S1 S2 S3 | spread since last reset (~5s)"));
}

void loop() {
  int v[3];
  for (uint8_t i = 0; i < 3; i++) {
    v[i] = readSensor(PINS[i]);
    if (first || v[i] < mn[i]) mn[i] = v[i];
    if (first || v[i] > mx[i]) mx[i] = v[i];
  }
  first = false;

  char buf[80];
  snprintf(buf, sizeof(buf),
    "S1=%4d S2=%4d S3=%4d | spread: %d %d %d",
    v[0], v[1], v[2],
    mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]);
  Serial.println(buf);

  // reset the min/max window every ~5 s so spread reflects recent noise
  if (millis() - lastReset > 5000) {
    for (uint8_t i = 0; i < 3; i++) { mn[i] = v[i]; mx[i] = v[i]; }
    lastReset = millis();
    Serial.println(F("-- window reset --"));
  }

  delay(1000);   // 1 Hz — deliberate for the eyeball test (see header note)
}

/*
 * gated_sensor_test.ino
 * Oyster_gape / nano_logger / deployment — power analysis State 5
 * GPIO-gated sensors: power the 3 sensors ON only during the read, then OFF.
 *
 * Wiring: S1 VDD=D4 OUT=A0 | S2 VDD=D5 OUT=A1 | S3 VDD=D6 OUT=A2 | all GND=GND
 * One sensor per pin (~7.5 mA) is within the ATmega 20 mA/pin limit. Deployment
 * will move to MOSFET/load-switch gating.
 * Result: 3 sensors ungated 39 mA -> gated 16 mA (asleep). LIBRARY: LowPower.
 */
#include <LowPower.h>

const uint8_t SENSOR_PWR[3] = { 4, 5, 6 };
const uint8_t SENSOR_SIG[3] = { A0, A1, A2 };
const uint16_t SETTLE_MS = 3;

int readSensor(uint8_t pin){ analogRead(pin); return analogRead(pin); }

void setup() {
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(SENSOR_PWR[i], OUTPUT);
    digitalWrite(SENSOR_PWR[i], LOW);
  }
}

void loop() {
  for (uint8_t i = 0; i < 3; i++) digitalWrite(SENSOR_PWR[i], HIGH);
  delay(SETTLE_MS);
  for (uint8_t i = 0; i < 3; i++) readSensor(SENSOR_SIG[i]);
  for (uint8_t i = 0; i < 3; i++) digitalWrite(SENSOR_PWR[i], LOW);
  LowPower.powerDown(SLEEP_120MS, ADC_OFF, BOD_OFF);
}

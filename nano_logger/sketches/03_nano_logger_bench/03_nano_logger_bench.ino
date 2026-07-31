/*
 * 03_nano_logger_bench.ino
 * Oyster_gape / nano_logger — full logger, BENCH build (step 3)
 *
 * Combines the three separately-verified subsystems into one logger:
 *   - 6 Hall sensors on A0,A1,A2,A3,A6,A7  (5 V native, NO divider; settling double-read)
 *   - DS3231 RTC on I2C (A4/A5) for wall-clock time
 *   - microSD on SPI (CS=D10) for the authoritative CSV log
 * Reads all six channels at 10 Hz, stamps each row, writes to SD, echoes to Serial.
 *
 * NO-COIN-CELL DESIGN:
 *   The RTC has no backup cell, so it loses absolute time on every power drop.
 *   We do NOT auto-set to compile time on boot (that would stamp stale dates
 *   after a field reboot). Instead each row carries time_valid:
 *       time_valid = 1  -> RTC has been anchored this power cycle (trust iso/unix)
 *       time_valid = 0  -> not anchored; iso/unix are placeholder, use ms only
 *   millis() (ms) is ALWAYS a valid fine relative axis. Absolute time is
 *   anchored explicitly (bench: send 's'; deployment: set from an external
 *   source at deploy time). A coin cell, if fitted later, makes lostPower()
 *   false after an outage so time_valid starts at 1 — no code change needed.
 *
 * CSV SCHEMA (final, multi-node ready):
 *   iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6
 *
 * LIBRARIES: RTClib by Adafruit; SD, SPI, Wire (bundled).
 * SERIAL COMMAND: 's' -> anchor RTC to compile time (bench convenience only).
 *
 * WIRING (see nano_logger/wiring/pinmap.md):
 *   Sensors: S1=A0 S2=A1 S3=A2 S4=A3 S5=A6 S6=A7   RTC: SDA=A4 SCL=A5
 *   SD: CS=D10 MOSI=D11 MISO=D12 SCK=D13           all VCC=5V, GND=GND
 *   NOTE: D13 = SCK and the onboard L LED; do not use the LED for status.
 *
 * FLOATING-PIN NOTE (bench only): unconnected analog pins "ghost" the last
 * driven channel sampled before them (shared ADC sample-and-hold). Harmless;
 * disappears when all 6 pins have sensors. A channel that suddenly correlates
 * ~1.0 with the channel read just before it is the signature of a disconnected
 * sensor — useful field diagnostic.
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>

#define NODE_ID            "N1"
const uint8_t  SENSOR_PINS[6] = { A0, A1, A2, A3, A6, A7 };
const uint8_t  NUM_SENSORS     = 6;
const uint8_t  SD_CS_PIN       = 10;
const unsigned long SAMPLE_INTERVAL_MS = 100;   // 10 Hz
const unsigned long SERIAL_BAUD        = 9600;

RTC_DS3231    rtc;
File          logFile;
char          logName[13];
bool          timeAnchored = false;
unsigned long sampleCount  = 0;
unsigned long nextSampleAt = 0;

int readSensor(uint8_t pin) {
  analogRead(pin);          // throwaway: let ADC input settle
  return analogRead(pin);   // real reading
}

void fatal(const char *msg) {
  while (true) { Serial.print(F("FATAL: ")); Serial.println(msg); delay(2000); }
}

void makeLogName() {
  for (int i = 1; i <= 9999; i++) {
    snprintf(logName, sizeof(logName), "LOG%04d.CSV", i);
    if (!SD.exists(logName)) return;
  }
  fatal("no free log filename (LOG9999 reached)");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 3000) {}

  Serial.println(F("\nOyster_gape Nano logger (step 3, bench)"));
  Wire.begin();

  if (!rtc.begin()) fatal("DS3231 not found (check A4/A5, power)");
  if (rtc.lostPower()) {
    Serial.println(F("RTC not anchored (no backup cell). Logging with time_valid=0."));
    Serial.println(F("Send 's' to anchor absolute time."));
    timeAnchored = false;
  } else {
    Serial.println(F("RTC retained time -> anchored."));
    timeAnchored = true;
  }

  Serial.print(F("Init SD... "));
  if (!SD.begin(SD_CS_PIN)) fatal("SD init failed (CS=D10, wiring, FAT32 card)");
  Serial.println(F("ok"));

  makeLogName();
  logFile = SD.open(logName, FILE_WRITE);
  if (!logFile) fatal("could not open log file");
  logFile.println(F("iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6"));
  logFile.flush();

  Serial.print(F("Logging to ")); Serial.print(logName); Serial.println(F("  @ 10 Hz"));
  Serial.println(F("iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6"));

  nextSampleAt = millis();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      timeAnchored = true;
      Serial.println(F(">> RTC anchored to compile time; time_valid now 1."));
    }
  }

  unsigned long now = millis();
  if ((long)(now - nextSampleAt) < 0) return;
  nextSampleAt += SAMPLE_INTERVAL_MS;

  int vals[NUM_SENSORS];
  for (uint8_t i = 0; i < NUM_SENSORS; i++) vals[i] = readSensor(SENSOR_PINS[i]);

  DateTime t = rtc.now();
  char iso[20];
  snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02u",
           t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  unsigned long unixt = t.unixtime();
  sampleCount++;

  char row[110];
  snprintf(row, sizeof(row),
           "%s,%lu,%lu,%d,%s,%lu,%d,%d,%d,%d,%d,%d",
           iso, unixt, now, timeAnchored ? 1 : 0, NODE_ID, sampleCount,
           vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]);

  logFile.println(row);
  logFile.flush();
  Serial.println(row);
}

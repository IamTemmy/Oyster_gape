/*
 * 01_rtc_test.ino  —  Oyster_gape / nano_logger  (bench test step 1, no-coin-cell design)
 * Confirms the DS3231 reads and increments over I2C, reports whether its time
 * is "anchored" (trustworthy absolute) or only running relative, and prints
 * millis() as the always-valid fine relative axis.
 *
 * DESIGN NOTE: this module deploys WITHOUT a backup coin cell, so the RTC loses
 * absolute time on every power drop. We do NOT auto-set to compile time (that
 * would stamp stale dates after a field reboot). Instead we detect the
 * lost-power state, expose it as time_valid, and set only on explicit command.
 *
 * Wiring: SDA=A4  SCL=A5  VCC=5V  GND=GND
 * Library: RTClib by Adafruit (NOT the similarly-named DS3231 library)
 * Serial command: 's' -> set RTC to compile time (bench convenience only)
 */
#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;
bool timeAnchored = false;

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 3000) {}

  if (!rtc.begin()) {
    Serial.println(F("DS3231 NOT found - check SDA/SCL/power"));
    while (1) delay(1000);
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC powered up WITHOUT valid absolute time (no backup cell)."));
    Serial.println(F("Time is running but NOT anchored. Send 's' to set it."));
    timeAnchored = false;
  } else {
    Serial.println(F("RTC reports retained time (a backup source is present)."));
    timeAnchored = true;
  }
  Serial.println(F("Ticking (time_valid shows anchor state):"));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      timeAnchored = true;
      Serial.println(F(">> RTC set to compile time; time now anchored."));
    }
  }

  DateTime t = rtc.now();
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u  time_valid=%d",
           t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second(),
           timeAnchored ? 1 : 0);
  Serial.print(buf);
  Serial.print(F("  ms="));
  Serial.println(millis());
  delay(1000);
}

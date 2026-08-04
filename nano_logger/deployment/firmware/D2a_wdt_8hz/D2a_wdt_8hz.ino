/*
 * D2a_wdt_8hz.ino
 * Oyster_gape / nano_logger / deployment — D2 v2 OPTION A
 * Watchdog-timer DEEP SLEEP, ~8 Hz (125 ms wake step).
 *
 * APPROACH (per supervisor): the ATmega's own watchdog timer wakes the CPU
 * from SLEEP_MODE_PWR_DOWN. No RTC alarm used for pacing. RTC (DS3231) is read
 * only for the wall-clock TIMESTAMP on each wake. Deepest sleep available.
 *
 * RATE: watchdog steps are fixed; there is no exact 100 ms. Nearest is 120 ms
 *   (LowPower SLEEP_120MS) -> ~8.3 Hz. Comfortably above the ~3 Hz the spawning
 *   FFT needs, while giving the deepest power savings.
 *
 * NOTE: real sample interval = 120 ms sleep + the few ms spent reading/writing.
 *   The 'ms' column (millis()) still records the true elapsed time per row, so
 *   the actual cadence is measured, not assumed. (millis() does not advance
 *   during power-down sleep, so we also keep a software sample counter.)
 *
 * WIRING: sensors A0..A3,A6,A7; RTC SDA=A4 SCL=A5; SD CS=D10; all 5V/GND.
 *   (SQW->D2 not required in this version.)
 * LIBRARIES: RTClib (Adafruit), LowPower (Rocket Scream, ATmega328P), SD/SPI/Wire.
 * SERIAL: unreliable across sleep (expected). 's' anchors RTC time.
 * SCHEMA: iso_time,unix,ms,time_valid,node,sample,s1..s6
 */
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <LowPower.h>

#define NODE_ID "N1"
const uint8_t SENSOR_PINS[6] = { A0, A1, A2, A3, A6, A7 };
const uint8_t NUM_SENSORS = 6;
const uint8_t SD_CS_PIN   = 10;
const unsigned long SERIAL_BAUD = 9600;

RTC_DS3231 rtc;
File logFile;
char logName[13];
bool timeAnchored = false;
unsigned long sampleCount = 0;

int readSensor(uint8_t pin){ analogRead(pin); return analogRead(pin); }
void fatal(const char*m){ while(1){ Serial.print(F("FATAL: ")); Serial.println(m); delay(2000);} }
void makeLogName(){ for(int i=1;i<=9999;i++){ snprintf(logName,sizeof(logName),"LOG%04d.CSV",i); if(!SD.exists(logName)) return;} fatal("no free log name"); }

void setup(){
  Serial.begin(SERIAL_BAUD);
  while(!Serial && millis()<3000){}
  Serial.println(F("\nD2a WDT deep-sleep ~8Hz"));
  Wire.begin();
  if(!rtc.begin()) fatal("DS3231 not found");
  timeAnchored = !rtc.lostPower();
  if(!timeAnchored) Serial.println(F("RTC not anchored. time_valid=0. Send 's'."));
  Serial.print(F("Init SD... "));
  if(!SD.begin(SD_CS_PIN)) fatal("SD init failed");
  Serial.println(F("ok"));
  makeLogName();
  logFile = SD.open(logName, FILE_WRITE);
  if(!logFile) fatal("open log failed");
  logFile.println(F("iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6"));
  logFile.flush();
  Serial.print(F("Logging to ")); Serial.println(logName);
}

void takeSample(){
  int v[NUM_SENSORS];
  for(uint8_t i=0;i<NUM_SENSORS;i++) v[i]=readSensor(SENSOR_PINS[i]);
  DateTime t=rtc.now();
  char iso[20];
  snprintf(iso,sizeof(iso),"%04u-%02u-%02uT%02u:%02u:%02u",t.year(),t.month(),t.day(),t.hour(),t.minute(),t.second());
  sampleCount++;
  char row[110];
  snprintf(row,sizeof(row),"%s,%lu,%lu,%d,%s,%lu,%d,%d,%d,%d,%d,%d",
    iso,(unsigned long)t.unixtime(),millis(),timeAnchored?1:0,NODE_ID,sampleCount,
    v[0],v[1],v[2],v[3],v[4],v[5]);
  logFile.println(row); logFile.flush();
  Serial.print(F("wake ")); Serial.println(sampleCount);
}

void loop(){
  if(Serial.available()){ char c=Serial.read(); if(c=='s'||c=='S'){ rtc.adjust(DateTime(F(__DATE__),F(__TIME__))); timeAnchored=true; Serial.println(F(">> anchored"));} }
  takeSample();
  // deepest sleep, watchdog wakes after ~120 ms
  LowPower.powerDown(SLEEP_120MS, ADC_OFF, BOD_OFF);
}

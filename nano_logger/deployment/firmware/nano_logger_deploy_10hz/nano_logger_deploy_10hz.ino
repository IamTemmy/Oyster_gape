/*
 * nano_logger_deploy_10hz.ino
 * Oyster_gape / nano_logger / deployment — DEPLOYMENT FIRMWARE
 *
 * Timer1 compare-match interrupt fires every 100 ms (exactly 10 Hz). On each
 * interrupt the CPU wakes from IDLE sleep, gates the 6 sensors on, reads them,
 * timestamps from the RTC, writes a CSV row to SD, and sleeps until the next.
 *
 * Merges three validated pieces: Timer1 100 ms interrupt + IDLE sleep (D2b),
 * 6-sensor GPIO gating (power test), RTC time_valid + SD logging (bench logger).
 *
 * WHY IDLE SLEEP: a hardware timer only wakes the CPU if its clock keeps
 * running, which rules out deepest power-down. IDLE is the deepest mode
 * compatible with EXACT 100 ms timing. Deepest sleep is only possible at the
 * watchdog's fixed steps (~8 Hz), not exactly 10 Hz. Measured: ~9 mA at 10 Hz
 * (IDLE) vs ~6 mA at 8 Hz (deep sleep) — the cost of exact 10 Hz.
 *
 * PINS: sensor power D4..D9 (gated, one per pin); signal A0,A1,A2,A3,A6,A7;
 *       RTC SDA=A4 SCL=A5; SD CS=D10 MOSI=D11 MISO=D12 SCK=D13; GND common.
 * SCHEMA: iso_time,unix,ms,time_valid,node,sample,s1..s6
 *   No-coin-cell: time_valid=0 until anchored (serial 's' / external at deploy);
 *   millis() is always a valid fine relative axis.
 * LIBS: RTClib, SD, SPI, Wire, avr/sleep.h. delay(3000) boot = reprogram window.
 */
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <avr/sleep.h>

#define NODE_ID "N1"
const uint8_t SENSOR_PWR[6] = { 4, 5, 6, 7, 8, 9 };
const uint8_t SENSOR_SIG[6] = { A0, A1, A2, A3, A6, A7 };
const uint8_t NUM_SENSORS = 6;
const uint8_t SD_CS_PIN   = 10;
const uint16_t SETTLE_MS  = 3;
const unsigned long SERIAL_BAUD = 9600;

RTC_DS3231 rtc;
File logFile;
char logName[13];
bool timeAnchored = false;
unsigned long sampleCount = 0;
volatile bool tick = false;

int readSensor(uint8_t pin){ analogRead(pin); return analogRead(pin); }
void fatal(const char*m){ while(1){ Serial.print(F("FATAL: ")); Serial.println(m); delay(2000);} }
void makeLogName(){ for(int i=1;i<=9999;i++){ snprintf(logName,sizeof(logName),"LOG%04d.CSV",i); if(!SD.exists(logName)) return;} fatal("no free log name"); }

void setupTimer1_100ms(){
  noInterrupts();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  OCR1A  = 1562;                         // 100 ms @ 16MHz/1024
  TCCR1B |= (1 << WGM12);                // CTC
  TCCR1B |= (1 << CS12) | (1 << CS10);   // prescaler 1024
  TIMSK1 |= (1 << OCIE1A);               // compare-match interrupt
  interrupts();
}
ISR(TIMER1_COMPA_vect){ tick = true; }

void setup(){
  Serial.begin(SERIAL_BAUD);
  while(!Serial && millis()<3000){}
  delay(3000);
  Serial.println(F("\nOyster_gape DEPLOYMENT logger — Timer1 10 Hz, gated sensors"));
  for(uint8_t i=0;i<NUM_SENSORS;i++){ pinMode(SENSOR_PWR[i], OUTPUT); digitalWrite(SENSOR_PWR[i], LOW); }
  Wire.begin();
  if(!rtc.begin()) fatal("DS3231 not found (A4/A5)");
  timeAnchored = !rtc.lostPower();
  if(!timeAnchored) Serial.println(F("RTC not anchored (no coin cell). time_valid=0. Send 's'."));
  Serial.print(F("Init SD... "));
  if(!SD.begin(SD_CS_PIN)) fatal("SD init failed (CS=D10, FAT32 card)");
  Serial.println(F("ok"));
  makeLogName();
  logFile = SD.open(logName, FILE_WRITE);
  if(!logFile) fatal("open log failed");
  logFile.println(F("iso_time,unix,ms,time_valid,node,sample,s1,s2,s3,s4,s5,s6"));
  logFile.flush();
  Serial.print(F("Logging to ")); Serial.println(logName);
  setupTimer1_100ms();
  set_sleep_mode(SLEEP_MODE_IDLE);
}

void takeSample(){
  for(uint8_t i=0;i<NUM_SENSORS;i++) digitalWrite(SENSOR_PWR[i], HIGH);
  delay(SETTLE_MS);
  int v[NUM_SENSORS];
  for(uint8_t i=0;i<NUM_SENSORS;i++) v[i] = readSensor(SENSOR_SIG[i]);
  for(uint8_t i=0;i<NUM_SENSORS;i++) digitalWrite(SENSOR_PWR[i], LOW);
  DateTime t = rtc.now();
  char iso[20];
  snprintf(iso,sizeof(iso),"%04u-%02u-%02uT%02u:%02u:%02u",t.year(),t.month(),t.day(),t.hour(),t.minute(),t.second());
  sampleCount++;
  char row[110];
  snprintf(row,sizeof(row),"%s,%lu,%lu,%d,%s,%lu,%d,%d,%d,%d,%d,%d",
    iso,(unsigned long)t.unixtime(),millis(),timeAnchored?1:0,NODE_ID,sampleCount,
    v[0],v[1],v[2],v[3],v[4],v[5]);
  logFile.println(row); logFile.flush();
}

void loop(){
  if(Serial.available()){ char c=Serial.read(); if(c=='s'||c=='S'){ rtc.adjust(DateTime(F(__DATE__),F(__TIME__))); timeAnchored=true; Serial.println(F(">> anchored")); } }
  while(!tick){ sleep_mode(); }
  tick = false;
  takeSample();
}

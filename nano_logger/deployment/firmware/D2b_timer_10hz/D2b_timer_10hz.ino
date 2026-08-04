/*
 * D2b_timer_10hz.ino
 * Oyster_gape / nano_logger / deployment — D2 v2 OPTION B
 * Hardware Timer1 interrupt, EXACT 100 ms (10 Hz), SLEEP_MODE_IDLE.
 *
 * APPROACH: Timer1 is configured to overflow every 100 ms and fire an ISR.
 * Between samples the CPU sleeps in IDLE mode (timer must keep running to
 * count, so full power-down is not possible here). Exact 10 Hz to match the
 * old system + spawning FFT, at the cost of a LIGHTER sleep than D2a.
 *
 * Trade vs D2a: exact 10 Hz but shallower sleep (less power saved). Measure
 * both to see how much the deeper WDT sleep is actually worth.
 *
 * WIRING/LIBS/SCHEMA identical to D2a. RTC = timestamps only.
 */
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <avr/sleep.h>

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
volatile bool tick = false;

int readSensor(uint8_t pin){ analogRead(pin); return analogRead(pin); }
void fatal(const char*m){ while(1){ Serial.print(F("FATAL: ")); Serial.println(m); delay(2000);} }
void makeLogName(){ for(int i=1;i<=9999;i++){ snprintf(logName,sizeof(logName),"LOG%04d.CSV",i); if(!SD.exists(logName)) return;} fatal("no free log name"); }

// Timer1: CTC mode, 100 ms. 16MHz/1024 prescaler = 15625 Hz; 100ms => 1562 counts.
void setupTimer1_100ms(){
  noInterrupts();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  OCR1A = 1562;                       // ~100 ms
  TCCR1B |= (1 << WGM12);             // CTC
  TCCR1B |= (1 << CS12) | (1 << CS10);// prescaler 1024
  TIMSK1 |= (1 << OCIE1A);            // compare-match interrupt
  interrupts();
}
ISR(TIMER1_COMPA_vect){ tick = true; }

void setup(){
  Serial.begin(SERIAL_BAUD);
  while(!Serial && millis()<3000){}
  Serial.println(F("\nD2b Timer1 exact 10Hz, SLEEP_IDLE"));
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
  setupTimer1_100ms();
  set_sleep_mode(SLEEP_MODE_IDLE);
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
  // sleep (IDLE) until Timer1 ISR sets tick
  while(!tick){ sleep_mode(); }
  tick = false;
  takeSample();
}

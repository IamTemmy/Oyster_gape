#include "BasicStepperDriver.h"

#define STEP_PIN 18
#define DIRECTION_PIN 19
#define SENSOR_PIN  4
#define READ_INTERVAL_MS 100
#define STEPS 200
#define RPM 640
#define MICROSTEPS 1

BasicStepperDriver stepper(STEPS, DIRECTION_PIN, STEP_PIN);
unsigned long readingTime = 0; // Variable to store the last time the sensor was read
unsigned long startTime = 0; // Variable to store the start time

void setup() {
  Serial.begin(115200);
  stepper.begin(RPM, MICROSTEPS);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIRECTION_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);
  Serial.println("Enter the number of steps to move.");
  Serial.println("Negative values will move in the opposite direction.");
  startTime = millis(); // Initialize the start time
}

void loop() {
  getStepCommand();
  readSensorPeriodically();
}

void getStepCommand(){
  if(Serial.available()){
    String serialString = Serial.readString();
    int stepValue = serialString.toInt();
    if(stepValue != 0){
      Serial.print("Value received is ");
      Serial.println(stepValue);
      stepper.move(stepValue * MICROSTEPS);
      readSensor();
      Serial.println("Enter another value to continue.");
    }
    else{
      Serial.println("ERROR: The value entered needs to be a non-zero integer.");
    }
  }
}

void readSensorPeriodically(){
  unsigned long currentMillis = millis();
  if(currentMillis - readingTime >= READ_INTERVAL_MS){
    readingTime = currentMillis;
    readSensor();
  }
}

void readSensor(){
  unsigned long elapsedTime = millis() - startTime; // Calculate elapsed time in milliseconds
  int reading = analogRead(SENSOR_PIN);
  
  Serial.print("Elapsed Time: ");
  Serial.print(elapsedTime / 1000); // Convert milliseconds to seconds
  Serial.print("s - Sensor reading is ");
  Serial.println(reading);
}

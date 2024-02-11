#include "BasicStepperDriver.h"

#define STEP_PIN 18
#define DIRECTION_PIN 19
#define SENSOR_PIN 4
#define READ_INTERVAL_MS 100
#define STEPS 200
#define RPM 640
#define MICROSTEPS 1

BasicStepperDriver stepper(STEPS, DIRECTION_PIN, STEP_PIN);
unsigned long readingTime;

void setup() {
  Serial.begin(115200);
  stepper.begin(RPM, MICROSTEPS);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIRECTION_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);
  Serial.println("Enter the number of steps to move.");
  Serial.println("Negative values will move the opposite direction.");
}

void loop() {
  getStepCommand();
  // readSensorPeriodically(); // No longer needed
}

void getStepCommand() {
  if (Serial.available()) {
    String serialString = Serial.readString();
    int stepValue = serialString.toInt();
    if (stepValue != 0) {
      Serial.print("Value received is ");
      Serial.println(stepValue);

      unsigned long startTime = millis(); // Measure start time
      stepper.move(stepValue * MICROSTEPS);
      while (stepper.isRunning()) { // Wait until movement finishes
        // Do nothing, or add optional progress indicators here
      }
      unsigned long endTime = millis(); // Measure end time

      readSensor();

      // Calculate and display elapsed time
      Serial.print("Time elapsed: ");
      Serial.print((endTime - startTime) / 1000.0);
      Serial.println(" seconds");

      Serial.println("Enter another value to continue.");
    } else {
      Serial.println("ERROR: The value entered needs to be a non-zero integer.");
    }
  }
}

void readSensor() {
  int reading = analogRead(SENSOR_PIN);
  Serial.print("Sensor reading is ");
  Serial.println(reading);
}

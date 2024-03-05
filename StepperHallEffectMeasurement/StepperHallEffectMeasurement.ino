#include "BasicStepperDriver.h"

#define STEP_PIN 18
#define DIRECTION_PIN 19
#define SENSOR_PIN 4
#define STEPS_PER_ROTATION 200
#define RPM 640
#define MICROSTEPS 1
BasicStepperDriver stepper(STEPS_PER_ROTATION, DIRECTION_PIN, STEP_PIN);

void setup() {
  Serial.begin(115200);
  stepper.begin(RPM, MICROSTEPS);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIRECTION_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);
}

void loop() {
  while (!Serial.available()) {};
  String input = Serial.readString();
  int inputValue = input.toInt();
  if (inputValue != 0) {
    stepper.move(inputValue);
  } else if (input.startsWith("start") || input.startsWith("Start")) {
    moveAndRead(-500, 100);
  }
}

void moveAndRead(int steps, int iterations) {
  Serial.println("Cm,Reading");
  printSensorReading(0, analogRead(SENSOR_PIN));
  for (int i = 0; i < iterations; i++) {
    stepper.move(steps);
    printSensorReading(abs(steps * 0.0001) * (i + 1), analogRead(SENSOR_PIN));
  }
}

void printSensorReading(float cms, int reading) {
  Serial.print(cms, 4);
  Serial.print(",");
  Serial.println(reading);
}
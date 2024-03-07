#include "BasicStepperDriver.h"
#include "Statistical.h"

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
    moveAndRead(-500, 100, 5);  //steps, iterations, readingSetSize
  }
}

void moveAndRead(int steps, int iterations, int readingSetSize) {
  Serial.print("Cms,");
  for (int i = 1; i <= readingSetSize; i++) {
    Serial.print("R");
    Serial.print(i);
    Serial.print(",");
  }
  Serial.println("Avg,SD");
  collectAndPrintStats(0, readingSetSize);
  for (int i = 1; i <= iterations; i++) {
    stepper.move(steps);
    float cms = abs(steps * .0001) * i;  //0.0001 cms per step
    collectAndPrintStats(cms, readingSetSize);
  }
}

void collectAndPrintStats(float cms, int readingSetSize) {
  Serial.print(cms, 3);
  Serial.print(",");
  int readingSet[readingSetSize];
  for (int i = 0; i < readingSetSize; i++) {
    delay(10);
    readingSet[i] = analogRead(SENSOR_PIN);
    Serial.print(readingSet[i]);
    Serial.print(",");
  }
  Array_Stats<int> readingSetStats(readingSet, readingSetSize);
  float average = readingSetStats.Average(readingSetStats.Arithmetic_Avg);
  Serial.print(average);
  Serial.print(",");
  float standardDeviation = readingSetStats.Standard_Deviation();
  Serial.println(standardDeviation, 3);
}
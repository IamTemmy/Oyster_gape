//code gives pulse input to rotate motor
#define STEP_PIN 18

#define ENABLE_PIN 19

void setup() {

  pinMode(STEP_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH);
}

void loop() {
  digitalWrite(STEP_PIN, HIGH);
  delay(1);
  
  digitalWrite(STEP_PIN, LOW);
  delay(1);
}

#define analogPin 4

void setup() {
  Serial.begin(115200);
  pinMode(analogPin, INPUT);
}

void loop() {
  int analogValue = analogRead(analogPin);

  Serial.print("Print the analog value: ");
  Serial.println(analogValue);

  delay(500);
}
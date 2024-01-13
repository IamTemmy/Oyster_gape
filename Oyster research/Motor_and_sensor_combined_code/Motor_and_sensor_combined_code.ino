// Define the ESP32 pins
#define STEP_PIN 18
#define ENABLE_PIN 19
#define ANALOG_PIN 4

// Set the interval for printing analog values (in milliseconds)
#define ANALOG_PRINT_INTERVAL 1000 // Adjust this value to your desired interval

unsigned long lastAnalogPrintTime = 0; // Variable to store the last time analog value was printed

void setup() {
  // Set the pins as digital output
  pinMode(STEP_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  // Enable the stepper motor
  digitalWrite(ENABLE_PIN, HIGH);

  // Initialize the serial monitor
  Serial.begin(115200);
}

void loop() {
  // Read the analog value
  int analogValue = analogRead(ANALOG_PIN);

  // Check if it's time to print the analog value
  unsigned long currentTime = millis();
  if (currentTime - lastAnalogPrintTime >= ANALOG_PRINT_INTERVAL) {
    // Print the analog value to the serial monitor
    Serial.print("Analog value: ");
    Serial.println(analogValue);

    // Update the last print time
    lastAnalogPrintTime = currentTime;
  }

  // Send a pulse to the stepper motor
  digitalWrite(STEP_PIN, HIGH);
  delay(1);
  digitalWrite(STEP_PIN, LOW);
  delay(1);
}

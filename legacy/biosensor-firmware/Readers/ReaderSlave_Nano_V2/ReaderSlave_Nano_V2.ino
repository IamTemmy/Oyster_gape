// Slave Nano with 6 sensors. Sends 12 bytes (6 X 10-bit numbers)
// Kamal S. Ali, 12/17/2020

#include <Wire.h>
#define byte_number 12
#define sensor_number 6
volatile short sensor_value[sensor_number];
volatile byte sensor[byte_number];

void setup() {
  //Serial.begin(9600);
  Wire.begin(1);                // join i2c bus with address 1
  Wire.onRequest(requestEvent);
}

void loop() {
  // wait for I2C request....
}

void requestEvent() {
  analogRead(0);
  sensor_value[0] = analogRead(0);
  analogRead(1);
  sensor_value[1] = analogRead(1);
  analogRead(2);
  sensor_value[2] = analogRead(2);
  analogRead(3);
  sensor_value[3] = analogRead(3);
  analogRead(6);
  sensor_value[4] = analogRead(6);
  analogRead(7);
  sensor_value[5] = analogRead(7);

  int i = 0;
  for (int j=0; j<6; j++) {
      sensor[i] = highByte(sensor_value[j]);
      //Serial.println(sensor[i]);
      sensor[i+1] = lowByte(sensor_value[j]);
      //Serial.println(sensor[i + 1]);
      i += 2;
    }
  Wire.write((byte*)sensor, byte_number); //write(array, length(in bytes))
}

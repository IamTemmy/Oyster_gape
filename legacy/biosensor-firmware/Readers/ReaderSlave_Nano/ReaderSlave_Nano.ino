/* Written by James Curtis Addy 
 *  
 *  This code makes the Nano behave as an i2c slave that
 *  collects readings on request and sends them to the
 *  master device.
*/

#include <Wire.h>

#define DATA_SIZE 12
#define I2C_ID 1

byte readings[DATA_SIZE];

void setup() {
  Wire.begin(I2C_ID);
  Wire.onRequest(read_and_send_on_request);
}

void loop() {  
}

void read_and_send_on_request() {
  analogRead(A0);
  int reading1 = analogRead(A0);
  
  analogRead(A1);
  int reading2 = analogRead(A1);
  
  analogRead(A2);
  int reading3 = analogRead(A2);

  analogRead(A3);
  int reading4 = analogRead(A3);
  
  analogRead(A6);
  int reading5 = analogRead(A6);
  
  analogRead(A7);
  int reading6 = analogRead(A7);
  
  readings[0] = get_MSB(reading1);
  readings[1] = get_LSB(reading1);
  
  readings[2] = get_MSB(reading2);
  readings[3] = get_LSB(reading2);
  
  readings[4] = get_MSB(reading3);
  readings[5] = get_LSB(reading3);
  
  readings[6] = get_MSB(reading4);
  readings[7] = get_LSB(reading4);
  
  readings[8] = get_MSB(reading5);
  readings[9] = get_LSB(reading5);
  
  readings[10] = get_MSB(reading6);
  readings[11] = get_LSB(reading6);
  
  Wire.write(readings, DATA_SIZE); 
}

byte get_MSB(int value)
{
  return (value >> 8) & 255;
}

byte get_LSB(int value)
{
  return value & 255;
}

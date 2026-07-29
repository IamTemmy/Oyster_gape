/* Written by James Curtis Addy 
 *  
 *  This code makes the Nano behave as an i2c master that
 *  collects readings every sample interval and then sends
 *  them to the target device when the buffer is full.
*/

#include <Wire.h>

#define DATA_SIZE 24 //Wire buffer size is only 32
#define SAMPLE_INTERVAL 100
#define I2C_ID 0

unsigned long start_time = 0;
unsigned int reading_index = 0;
byte readings[DATA_SIZE];

void setup() {
  Wire.begin();
  delay(100);
  start_time = millis();
}

void loop() {  
  if((millis() - start_time) >= SAMPLE_INTERVAL)
  {
    start_time = millis();      
    readings[reading_index++] = get_MSB(analogRead(A0));
    readings[reading_index++] = get_LSB(analogRead(A0));
    
    readings[reading_index++] = get_MSB(analogRead(A1));
    readings[reading_index++] = get_LSB(analogRead(A1));
    
    readings[reading_index++] = get_MSB(analogRead(A2));
    readings[reading_index++] = get_LSB(analogRead(A2));
    
    readings[reading_index++] = get_MSB(analogRead(A3));
    readings[reading_index++] = get_LSB(analogRead(A3));
    
    readings[reading_index++] = get_MSB(analogRead(A6));
    readings[reading_index++] = get_LSB(analogRead(A6));
    
    readings[reading_index++] = get_MSB(analogRead(A7));  
    readings[reading_index++] = get_LSB(analogRead(A7));
    
    if(reading_index >= DATA_SIZE)
    {
      reading_index = 0;
      Wire.beginTransmission(I2C_ID);     
      Wire.write(readings, DATA_SIZE);
      Wire.endTransmission();  
    }
  }
}

byte get_MSB(int value)
{
  return (value >> 8) & 255;
}

byte get_LSB(int value)
{
  return value & 255;
}

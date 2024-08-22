#include <MbraceAws.h>
#include <base64.h>

/*
  This Sketch requires that the device is provisioned with the Mbrace AWS account. 
  Upload the MbraceProvisioningWiFiDevice.ino Sketch first if this device is not yet provisioned.
*/

#define PUBLISH_DATA_TOPIC "mbrace/data"
#define DATETIME_REQUEST_TOPIC "mbrace/datetime/request"
#define DATETIME_RESPONSE_TOPIC "mbrace/datetime/response"
#define SEND_INTERVAL 5      // in seconds
#define SAMPLE_INTERVAL 100  //in milliseconds
#define SAMPLE_RATE 1000 / SAMPLE_INTERVAL
#define SENSOR_COUNT 6                                                   //number of sensors connected to analog pins
#define SENSOR_BUFFER_SIZE SEND_INTERVAL *SAMPLE_RATE *SENSOR_COUNT * 2  //Space to store readings in 2-byte groups
#define TIMESTAMP_BUFFER_SIZE SEND_INTERVAL *SAMPLE_RATE * 4             //Space to store timestamps in ms in 4-byte groups

uint8_t sensorReadings[SENSOR_BUFFER_SIZE];
uint8_t timestamps[TIMESTAMP_BUFFER_SIZE];
JsonDocument dataJsonDoc;
unsigned long sampleTime = 0;
unsigned int sensorBufferIndex = 0;
unsigned int timestampBufferIndex = 0;
/*
This is an array of the pin values for the sensors. 
Only the ADC1 pins can be used with WiFi connected. 
*/
int sensorPinValues[SENSOR_COUNT] = { 36, 39, 32, 33, 34, 35 };  //GPIO values of ADC1 pins
//int sensorPinValues[SENSOR_COUNT] = {A0,A3,A4,A5,A6,A7}; //aliased values of ADC1 pins

void sampleSensors() {
  unsigned long currentMillis = millis();
  if (currentMillis - sampleTime >= SAMPLE_INTERVAL) {
    setTimestamp(currentMillis);
    sampleTime = currentMillis;
    for (int i = 0; i < SENSOR_COUNT; i++) {
      //unsigned short value = analogRead(sensorPinValues[i]);
      unsigned short value = i; //test values
      uint8_t msb = highByte(value);
      uint8_t lsb = lowByte(value);
      sensorReadings[sensorBufferIndex++] = msb;
      sensorReadings[sensorBufferIndex++] = lsb;
    }
  }
}

void setTimestamp(unsigned long currentMillis) {
  timestamps[timestampBufferIndex++] = (currentMillis >> 24) & 255;
  timestamps[timestampBufferIndex++] = (currentMillis >> 16) & 255;
  timestamps[timestampBufferIndex++] = (currentMillis >> 8) & 255;
  timestamps[timestampBufferIndex++] = currentMillis & 255;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi is connected!");
}

void connectAWS(const char *clientId, const char *certificate, const char *privateKey) {
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(certificate);
  net.setPrivateKey(privateKey);
  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  Serial.print("Connecting to ");
  Serial.print(clientId);
  Serial.print("...");
  while (!client.connect(clientId)) {
    Serial.print(".");
    delay(100);
  }
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  Serial.println("AWS IoT Connected!");
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic);
  Serial.println(payload);
  if (topic == DATETIME_RESPONSE_TOPIC) {
    /*
      The id is the start date and time combined with the device mac address.
      The start date and time are sent in the payload for this topic. 
    */
    dataJsonDoc["id"] = payload + "__" + WiFi.macAddress();
  }
}

void publishData() {
  dataJsonDoc["ms"] = base64::encode(timestamps, timestampBufferIndex);
  dataJsonDoc["data"] = base64::encode(sensorReadings, sensorBufferIndex);
  String serializedData;
  serializeJson(dataJsonDoc, serializedData);
  Serial.print("Publishing: ");
  Serial.println(serializedData);
  client.publish(PUBLISH_DATA_TOPIC, serializedData);
  /*Reset sensor and timestamp indices*/
  sensorBufferIndex = 0;
  timestampBufferIndex = 0;
}

void connectDevice() {
  File file = SPIFFS.open(CERT_FILENAME);
  if (!file) {
    Serial.println("Unable to open cert file.");
    return;
  }
  uint8_t contents[MQTT_BUFFER_SIZE];
  file.read(contents, MQTT_BUFFER_SIZE);
  file.close();
  Serial.println("Contents of connect info file:");
  Serial.println((char *)contents);
  deserializeJson(jsonDoc, contents);
  connectAWS(jsonDoc["thingName"], jsonDoc["certificatePem"], jsonDoc["privateKey"]);
  //client.subscribe("mbrace/test"); //for testing messages recieved from AWS IoT Core
}

void initializeSensorPins() {
  for (int i = 0; i < SENSOR_COUNT; i++) {
    pinMode(sensorPinValues[i], INPUT);
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Enter a key to begin");
  while (!Serial.available()) {}
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
    return;
  }
  connectWiFi();
  if (SPIFFS.exists(CERT_FILENAME)) {
    Serial.println("Certification file exists. Connecting device to AWS IoT...");
    connectDevice();
    SPIFFS.end();
  } else {
    Serial.println("No certification file exists. Device needs to be provisioned");
    while (true) {}
  }
  client.onMessage(messageHandler);
  dataJsonDoc["count"] = SENSOR_COUNT;
  String dateTimeJson = "{\"topic\":\"" + String(DATETIME_RESPONSE_TOPIC) + "\"}";
  client.publish(DATETIME_REQUEST_TOPIC, dateTimeJson);  //Request date and time from AWS
  client.subscribe(DATETIME_RESPONSE_TOPIC);
  Serial.println("Awaiting AWS response for the start date and time to set id...");
  while (!dataJsonDoc.containsKey("id")) {
    //Wait for date and time value
    client.loop();
  }
  Serial.print("id is set to ");
  Serial.println(String(dataJsonDoc["id"]));
  initializeSensorPins();
  //test
  // for (int i = 0; i < 5; i++) {
  //   sampleSensors();
  //   delay(2000);
  // }
  // publishData();
  // Serial.println("Message published.");
}

void loop() {
  client.loop();
  sampleSensors();
  if (sensorBufferIndex >= SENSOR_BUFFER_SIZE) {
    publishData();
    Serial.println("Data published");
  }
}

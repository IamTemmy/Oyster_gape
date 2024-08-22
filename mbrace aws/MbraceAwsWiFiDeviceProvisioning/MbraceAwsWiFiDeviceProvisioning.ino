#include <MbraceAws.h>

/*
  Upload this sketch to provision this device with a unique AWS certificate.
  This certificate will be stored on this device in a file using SPIFFS.
*/

#define AWS_IOT_CERTIFICATE_CREATE_REQUEST "$aws/certificates/create/json"
#define AWS_IOT_CERTIFICATE_CREATE_ACCEPTED "$aws/certificates/create/json/accepted"
#define AWS_IOT_CERTIFICATE_CREATE_REJECTED "$aws/certificates/create/json/rejected"
/*MbraceProvisioning is the name of the provisioning template defined on the AWS account*/
#define AWS_IOT_REGISTER_REQUEST "$aws/provisioning-templates/MbraceProvisioning/provision/json"
#define AWS_IOT_REGISTER_ACCEPTED "$aws/provisioning-templates/MbraceProvisioning/provision/json/accepted"
#define AWS_IOT_REGISTER_REJECTED "$aws/provisioning-templates/MbraceProvisioning/provision/json/rejected"

// Claim Certificate
const char AWS_CERT_CRT[] PROGMEM = R"(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUBckrqEA0nZMRoW65Zr/Fwz6CwCswDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI0MDgwNzIwMjk0
MVoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAM2vfJ9YapC/HOcR+MOo
1U3mM3lbB1yQddI6/CdN8wWX7cxsKF+fdY3HXEdhQ4ctKtssp5UBat7KesZ0o20U
0OOjVLnHuRcfglPIfZihzwV77YOudNf/yiciBgkYz9iAYygf7JDrloj3CRWHWb79
qbb7jZObLvyHZuvdAVDkpVTQTAX6sOdEWtFIP+LdJu8SJmycUV7BdGDKJTYaEXWS
8xlhXLUzPNmaaMw2r/9JTu5gdfYoGeJ1mvcGfHpGb0VhAgFdJ/nZU1p/TSsRO5tx
cYgy7Y8NvKj963LRbDwW+mnfbH+oO7AReNPBu11S/zzhc7F3VIj4GKk3bDRrIBvX
CSsCAwEAAaNgMF4wHwYDVR0jBBgwFoAUmi/AS3Xb19kbRl1T09kn4nNyq1UwHQYD
VR0OBBYEFKrA9VmaHYhwX6AWtlYucJ3I8ZmoMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBjfRIljHXrcRex4rGmhz6jJbqx
3ik+xpZVPrnEyyyfoFXluGKURFI88cyKihBcszu2FoMSPLz2OOGJQ1VBh//MOb2l
dTpSSpu9Dvrrt0U+Dv20aEjrtAmz+a6oteW44KGKNkbGnB0WU0UE3SMezuU8Y9iT
f6Khc0DW2s+8cTG5ULB0krHcpy5W1uURG+LOTJYlaQrQgmXtlC11aI08Jz0kMuHy
YnzUNGcM4dW3wLCmyBc79isaekdb40b8tE+Rzr9rstrKGZSh+67bfVAFpgktYjSU
8kY+Q2HwP/h+kOOn96sbHXycHSg6yT89vv7D4VRSpi1sXVKbHsBSC1Ax6O7q
-----END CERTIFICATE-----
)";

// Claim Private Key
const char AWS_CERT_PRIVATE[] PROGMEM = R"(
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAza98n1hqkL8c5xH4w6jVTeYzeVsHXJB10jr8J03zBZftzGwo
X591jcdcR2FDhy0q2yynlQFq3sp6xnSjbRTQ46NUuce5Fx+CU8h9mKHPBXvtg650
1//KJyIGCRjP2IBjKB/skOuWiPcJFYdZvv2ptvuNk5su/Idm690BUOSlVNBMBfqw
50Ra0Ug/4t0m7xImbJxRXsF0YMolNhoRdZLzGWFctTM82ZpozDav/0lO7mB19igZ
4nWa9wZ8ekZvRWECAV0n+dlTWn9NKxE7m3FxiDLtjw28qP3rctFsPBb6ad9sf6g7
sBF408G7XVL/POFzsXdUiPgYqTdsNGsgG9cJKwIDAQABAoIBAC8/FjNMDf59x9fU
Kv5Ws9iW/k/r9v7uOAI4hl9I6n/obDk+xu9gI5KTrsC+uNI+L8/0q/HwR2oxvI4F
kNynoWwIwpeCJyQGmts//Imo8XLjCqXq1vAe04K1sAk659NlemFnI+IOG6AOjsmh
M55Jikh79ANp7BsZxyx6saixPMEERkziBggiGvywuWRDOmBrvnBf2j/oNdFarbGS
1O71hCf6SOpIWR0pU4PITEFz/88Znbyf4qbPYNot7K/iayU8qrdL5kFyUt/6+HYy
7LarmtB55yV5OmOT8JajlwCnFeQDOBJIk6+F1iB5gXzTOgO9nHkGR+fOCGlPBhRV
oikdADECgYEA77lgzWwbBHX+bu4XPcqbQMxUvWyJ0e7pqAkH2BmyskajZtbA2Abt
+KhZva4O559kWPzzlGH66OEtaI9kwa0eJaZpOs0bE/he0Fuo+rG+/APeAJrLy14N
N9at+mkXrjlkooOadJTlT1msh+sSCw+n9DD/2sX6HgwehldFyJ6ux2cCgYEA26Z8
hYpT98rzdg/v7/NZxN8gdGwSDqQRjIK8VoKBNTM5M2KOxOl8xvws8ITaMwPdUa+u
FrdwvHdYD0uW+QC2Ofzm+daEA/YK/DHyfgmPVaW33qVp+cgg9dG8kVkifVm3Sv8R
3A4e1fDa6CywdhTyeswpgcC25HvYHhhC/adG6Z0CgYA9XS6yZuHjmnCu5LN+Vca4
J5Xph0cgPhABu44Oe0WK7RoW0RI8OkngRPfz3gJiuCJvxRB1Az+/LST6hvo7uZzl
9lspeidcTU+39j6jLay9xh+l9/oC7OhlSsbuOsidCIQCNb3r6dFpJoNLp5jOzAsq
LwDPd6420tNdgCmU2UnUMQKBgErdDrnxMLdXK+3EdtIAzkrkhcpIf0sQLo8GjCys
JF++irNx1xlUP1wO3T5I+ZnDqm5KA3rooPsLbi8gY1+RF4riINNsguhattnIKE8+
8OSPLAEtvdYNmPZPuwaLK88vgeKE11B6W5Ytll7lxGsqro6eAVOhHHT5pOp0+Hg9
yFSdAoGBAJgzJ7e714ciFw6iEeVJZYS3I3KwoFJylOYGQ3I5/f0Ak4y0m2LwQcma
yVDPYuvYACu0X55elVZkwfzw+msugtrcaQqK9bxwz3uJn1TQ2FGJ8VFU0Y9LLP/R
q4+eUkJQTifWoy9AGEpfqTLzBBf9v0lzr2hplRZoOuRBjmy1xgRN
-----END RSA PRIVATE KEY-----
)";

void onCertificateCreateAccepted(const char *payload) {
  Serial.println("Certificate created.");
  DynamicJsonDocument certJson(MQTT_BUFFER_SIZE);
  Serial.println("certJson created.");
  deserializeJson(certJson, payload);
  Serial.println("payload deserialized.");
  jsonDoc["certificatePem"] = certJson["certificatePem"];
  jsonDoc["privateKey"] = certJson["privateKey"];
  const char *certificateOwnershipToken = certJson["certificateOwnershipToken"];
  DynamicJsonDocument registerJson(2048);
  registerJson["certificateOwnershipToken"] = certificateOwnershipToken;
  JsonObject parameters = registerJson.createNestedObject("parameters");
  parameters["SerialNumber"] = WiFi.macAddress();
  char registerRequest[2048];
  serializeJson(registerJson, registerRequest);
  Serial.println("Register request:");
  Serial.println(registerRequest);
  client.publish(AWS_IOT_REGISTER_REQUEST, registerRequest);
  client.subscribe(AWS_IOT_REGISTER_ACCEPTED);
  client.subscribe(AWS_IOT_REGISTER_REJECTED);
  Serial.println("Registering device to AWS IoT.");
}

void onRegisterAccepted(const char *payload) {
  StaticJsonDocument<512> payloadJson;
  deserializeJson(payloadJson, payload);
  jsonDoc["thingName"] = payloadJson["thingName"];
  char connectInfo[MQTT_BUFFER_SIZE];
  serializeJson(jsonDoc, connectInfo);
  File file = SPIFFS.open(CERT_FILENAME, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open/create cert file.");
    return;
  }
  file.print(connectInfo);
  file.close();
  Serial.println("Device is registered. Connection info is saved.");
  SPIFFS.end();
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic);
  Serial.println(payload);
  if (topic == AWS_IOT_CERTIFICATE_CREATE_ACCEPTED) {
    onCertificateCreateAccepted(payload.c_str());
    return;
  }
  if (topic == AWS_IOT_REGISTER_ACCEPTED) {
    onRegisterAccepted(payload.c_str());
    return;
  }
}

void provisionDevice() {
  connectAWS("TestThing", AWS_CERT_CRT, AWS_CERT_PRIVATE);
  Serial.println("Subscribing to create response topics");
  client.subscribe(AWS_IOT_CERTIFICATE_CREATE_ACCEPTED);
  client.subscribe(AWS_IOT_CERTIFICATE_CREATE_REJECTED);
  Serial.println("Publishing certificate request");
  client.publish(AWS_IOT_CERTIFICATE_CREATE_REQUEST);
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

void setup() {
  Serial.begin(9600);
  Serial.println("Enter a key to begin");
  while (!Serial.available()) {}
  while(Serial.available()){
    Serial.print(Serial.read());
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
    return;
  } 
  bool provision = false;
  if (SPIFFS.exists(CERT_FILENAME)) {
    Serial.println("A certification file already exists on this board.");
    Serial.println("Do you want to overwrite the existing certification?");
    Serial.println("Enter y or Y to overwrite existing file:");
    while(!Serial.available()){}
    String reply =  Serial.readString();
    Serial.println(reply);
    if( reply == "y" || reply == "Y"){
      provision = true;
      Serial.println("Overwrite request confirmed");
    }
    else{
      SPIFFS.end();
    }
  } else {
    Serial.println("No certification file exists.");
    provision = true;
  }
  if(provision){
    connectWiFi();
    Serial.println("Provisioning device...");
    provisionDevice();
  }
  client.onMessage(messageHandler);
}

void loop() {
  client.loop();
}
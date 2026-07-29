/* Written by James Curtis Addy

  This code reads the analog pins of an ESP32 and 
  sends the data to a server when the buffer is full.
*/

#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks
#include <WiFi.h>
#include "Base64Encode.h"
#include <SD.h>

#define DATA_SIZE 128 //120 bytes for readings and 8 for timestamp {{TTTT}}
const unsigned int BYTES_PER_DAY = DATA_SIZE * 3600 * 24; //Day's worth of bytes

//Change this stuff////
const char* ssid = "";//network name
const char* wifi_password =  ""; //password
const char* wifi_username = ""; //for WPA2 Enterprise networks
const char* experiment_start_date = "2021-02-13"; //This date will be used for the SD file name
const char* server_directory = "/GCRL_Exp2";
///////////////////////

WiFiClient client;
byte data_array[DATA_SIZE];
size_t data_index = 8; //Start after the timestamp
const char server[] = "mbrace.xyz";
char file_name[20] = ""; //This will be the mac address acquired from WiFi.macAddress()
char sd_folder_name[64] = "";
char server_subdirectory[64] = "";

int port = 80;

//SD
unsigned int sd_file_counter = 0;
char sd_file_path_string[128] = "";
char sd_file_suffix[64] = "";

File sd_file;
unsigned long bytes_written = 0;

unsigned long get_readings_time = 0;

uint8_t reconnect_count = 0;

unsigned int post_request_content_length;
int encoded_length;

void setup()
{
  Serial.begin(9600);
  delay(100);  // Short delay, wait for the Mate to send back CMD
  client.setTimeout(1);

  while (!check_connection())
  {
    delay(5000);
  }


  data_array[0] = '{';
  data_array[1] = '{';
  data_array[6] = '}';
  data_array[7] = '}';

  Serial.println("Setting file and folder names");
  strcpy(file_name, WiFi.macAddress().c_str());
  replace_char(file_name, ':', '_');
  Serial.print("File name:");
  Serial.println(file_name);
  strcpy(server_subdirectory, "Esp32ReaderTest_");
  strcat(server_subdirectory, file_name);
  Serial.print("Server subdirectory: ");
  Serial.println(server_subdirectory);
  strcpy(sd_folder_name, "/");
  strcat(sd_folder_name, server_subdirectory);
  Serial.print("SD folder name:");
  Serial.println(sd_folder_name);
  //Begin SD object (mount to SD card)
  Serial.println("Mounting SD Card");
  while (!SD.begin(5))
  {
    Serial.println(F("SD mount failed"));
    delay(2000);
  }
  Serial.println(F("SD Card Mount Succeeded"));
  initialize_sd_file();

  encoded_length = base64_encoded_length(DATA_SIZE);

  //JSON structure:
  //{"f":"FOLDER", "n":"NAME", "d":"DATA"}
  int field_count = 3;
  post_request_content_length = (field_count * 7) + //based on the JSON structure above
                                field_count + //for f,n,d
                                strlen(server_subdirectory) +
                                strlen(file_name) +
                                encoded_length;
}

void loop()
{
  if (data_index >= DATA_SIZE)
  {
    Serial.println(F("Data ready"));
    add_timestamp();
    update_sd_file();
    Serial.println(F("SD file updated"));
    char encoded_data[encoded_length];
    base64_encode(encoded_data, (char*)data_array, DATA_SIZE);
    data_index = 8; //Once the data is encoded the original data buffer is free to use.
    if (reconnect_count == 0 || WiFi.status() == WL_CONNECTED)
    {
      if (check_connection())
      {
        publish_post(encoded_data);
        Serial.println(F("Published data to server"));
      }
      else
      {
        reconnect_count = 4;//Try again in 5 seconds.
      }
    }
    else
    {
      reconnect_count--;
    }
  }

  if ((millis() - get_readings_time) >= 100)
  {
    get_readings_time = millis();
    collect_readings();
  }
}

void replace_char(char* string, char to_replace, char replace_with)
{
  Serial.println("Replace characters");
  int index = 0;
  while (string[index] != '\0')
  {
    if (string[index] == to_replace)
    {
      string[index] = replace_with;
    }
    index++;
  }
}

void collect_readings()
{
  if (data_index >= DATA_SIZE) return; //Don't read if the buffer is full
  short readings[6];
  analogRead(32);
  readings[0] = analogRead(32);
  analogRead(33);
  readings[1] = analogRead(33);
  analogRead(34);
  readings[2] = analogRead(34);
  analogRead(35);
  readings[3] = analogRead(35);
  analogRead(36);
  readings[4] = analogRead(36);
  analogRead(39);
  readings[5] = analogRead(39);

  for(int i = 0; i < 6; i++)
  {
    data_array[data_index] = highByte(readings[i]);
    data_index++;
    data_array[data_index] = lowByte(readings[i]);
    data_index++;
  }
}

bool check_connection()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    establish_wifi_connection();
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("WiFi is connected"));
    if (establish_server_connection())
    {
      return true;
    }
  }
  return false;
}

void add_timestamp()
{
  unsigned long current_millis = millis();
  data_array[2] = (current_millis >> 24) & 255;
  data_array[3] = (current_millis >> 16) & 255;
  data_array[4] = (current_millis >> 8) & 255;
  data_array[5] = current_millis & 255;
}

void establish_wifi_connection()
{
  Serial.print(F("Connecting to network: "));
  Serial.println(ssid);

  if (strcmp(wifi_username, "") != 0)
  {
    Serial.println("Enterprise connection");
    //Enterprise setup
    WiFi.mode(WIFI_STA); //init wifi mode
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)wifi_username, strlen(wifi_username)); //provide identity
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)wifi_username, strlen(wifi_username)); //provide username --> identity and username is same
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)wifi_password, strlen(wifi_password)); //provide password
    esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT(); //set config settings to default
    esp_wifi_sta_wpa2_ent_enable(&config); //set config settings to enable function
  }
  WiFi.begin(ssid, wifi_password);
}

bool establish_server_connection()
{
  /*Clear read buffer because connected() returns true
    even with no server connection if there is still data
    in the read buffer*/
  while (client.available())
  {
    client.read();
  }
  client.flush(); //Clears the client buffer

  if (!client.connected())
  {
    Serial.println(F("Connecting to server"));
    if (client.connect(server, port))
    {
      Serial.println(F("Connected to server"));
      return true;
    }
    else
    {
      Serial.println(F("Unable to connect to server"));
      return false;
    }
  }
  return true;
}

bool valid_send(size_t size_from_send)
{
  return size_from_send > 0;
}

bool publish_post(char* encoded_data)
{
  bool good_publish = valid_send(client.print(F("POST /"))) &&
                      valid_send(client.print(server_directory)) &&
                      valid_send(client.println(F("/create_data_files.php HTTP/1.1"))) &&
                      valid_send(client.println(F("HOST: mbrace.xyz"))) &&
                      valid_send(client.println(F("Content-Type: application/json"))) &&
                      valid_send(client.println(F("Connection: keep-alive"))) &&
                      valid_send(client.print(F("Content-Length: "))) &&
                      valid_send(client.println(post_request_content_length)) &&
                      valid_send(client.println()) &&
                      valid_send(client.print(F("{\"f\":\""))) &&
                      valid_send(client.print(server_subdirectory)) &&
                      valid_send(client.print(F("\", \"n\":\""))) &&
                      valid_send(client.print(file_name)) &&
                      valid_send(client.print(F("\", \"d\":\""))) &&
                      valid_send(client.print(encoded_data)) &&
                      valid_send(client.println(F("\"}")));
  return good_publish;
}

//SD functions
unsigned int get_file_count(const char* folder_path)
{
  File root = SD.open(folder_path);
  if (!root)
  {
    Serial.print(F("Failed to open "));
    Serial.println(folder_path);
    return 0;
  }
  if (!root.isDirectory())
  {
    Serial.print(folder_path);
    Serial.println(F(" is not a directory"));
    return 0;
  }
  int count = 0;
  File current_file = root.openNextFile();

  while (current_file)
  {
    if (!current_file.isDirectory())
    {
      count++;
    }
    current_file.close();
    current_file = root.openNextFile();
  }
  return count;
}

void update_sd_file()
{
  sd_file.write((uint8_t*)data_array, DATA_SIZE);
  sd_file.flush();
  bytes_written += DATA_SIZE;

  //Create a new file when bytes written is a day's amount.
  if (bytes_written >= BYTES_PER_DAY)
  {
    bytes_written = 0;
    sd_file_counter += 1;
    sd_file.close();
    set_sd_suffix_to_counter();
    update_sd_file_path_string();
    sd_file = SD.open(sd_file_path_string, FILE_WRITE);
  }
}

void set_sd_suffix_to_counter()
{
  char counter_string[20] = "";
  sprintf(counter_string, "%d", sd_file_counter);
  Serial.print("SD counter: ");
  Serial.println(counter_string);
  strcpy(sd_file_suffix, counter_string);
}

void initialize_sd_file()
{
  //Create SD folder if it doesn't exist on the card
  if (!SD.exists(sd_folder_name))
  {
    SD.mkdir(sd_folder_name);
  }
  //Set counter to the most recent file in the folder.
  sd_file_counter = get_file_count(sd_folder_name) + 1;

  set_sd_suffix_to_counter();

  if (sd_file_counter != 1)
  {
    //If there are already files in the folder, this must be a reset.
    strcat(sd_file_suffix, "_Reset");
  }

  //Initialize file
  update_sd_file_path_string();
  sd_file = SD.open(sd_file_path_string, FILE_WRITE);
}

void update_sd_file_path_string()
{
  //The mac address is already acquired in the server file name.
  strcpy(sd_file_path_string, sd_folder_name);
  strcat(sd_file_path_string, "/");
  strcat(sd_file_path_string, file_name);
  strcat(sd_file_path_string, "__");
  strcat(sd_file_path_string, experiment_start_date);
  strcat(sd_file_path_string, "__");
  strcat(sd_file_path_string, sd_file_suffix);
  strcat(sd_file_path_string, ".bin");
  //(sd_folder_name)/(file_name)__(experiment_start_date)__(sd_file_suffix).bin
}

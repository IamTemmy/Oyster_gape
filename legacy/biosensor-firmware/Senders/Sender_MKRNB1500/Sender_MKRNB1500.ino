/*Written by James Curtis Addy
 * 
  This code is designed to receive sensor values from the connected device (likely Nano)
  and fill up to 7200 bytes. Once this value is reached, a timestamp representing the 
  current milliseconds on the timer is added. The data is then sent to the mbrace.xyz
  file server based on the server directory, subdirectory, and file name. The server will then process the
  data and write it to binary and csv files.
*/

#include <MKRNB.h>
#include <Wire.h>
#include "Base64Encode.h"
#include <string.h>

NBClient client;
NB nbAccess;

#define DATA_SIZE 7208 //7200 for data + 8 for timestamp. 7200 bytes will fill in 60 seconds.
#define I2C_ID 0
#define SEND_TIMEOUT 40000 //40 seconds to attempt to send data.

///Change this stuff///////////////
const char* server_directory = "/Testing";
const char* server_subdirectory = "MKRNB1500"; 
const char* file_name = "MKRNB1500";
//////////////////

volatile byte data_array[DATA_SIZE];
volatile size_t data_index = 8; //The first 8 bytes are reserved for the timestamp
char server[] = "mbrace.xyz";
int port = 80;

unsigned int post_request_content_length;
int encoded_length;

void setup()
{
  Wire.begin(I2C_ID);
  Wire.onReceive(receive_data_event);
  Serial.begin(9600);
  while (!Serial) {}
  client.set_connect_timeout(5000);
  client.set_write_timeout(5000);
  nbAccess.setTimeout(5000);
  //These are dilimiters for the timestamp
  data_array[0] = '{';
  data_array[1] = '{';
  data_array[6] = '}';
  data_array[7] = '}';

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
    add_timestamp();
    char encoded_data[encoded_length];
    base64_encode(encoded_data, (char*)data_array, DATA_SIZE);
    data_index = 8; //Once the data is encoded the original data buffer is free to use.
    unsigned long start_millis = millis();
    
    connect_modem(start_millis);
    
    if (nbAccess.status() == NB_READY)
    {
      if (send_data_to_server(encoded_data, encoded_length, start_millis))
      {
          Serial.println("Send successful!");
      }
    }
    
    nbAccess.secureShutdown();
  }
}

void connect_modem(unsigned long start_millis)
{
    //Keep trying to connect the modem if it's not ready.
    do {
      nbAccess.begin(NULL, false, true);
    } while ((nbAccess.status() != NB_READY) && !get_is_timed_out(start_millis, SEND_TIMEOUT));
}

bool get_is_timed_out(unsigned long start_millis, unsigned int timeout)
{
  return (millis() - start_millis) >= timeout;
}

bool send_data_to_server(char* encoded_data, int encoded_length, unsigned long start_millis)
{
  while (!get_is_timed_out(start_millis, SEND_TIMEOUT))
  {
    while (client.available()) client.read(); //Clear read buffer
    if (client.connect(server, port))
    {      
      if (publish_post(encoded_data))
      {
        bool good_response = get_server_response();
        if (good_response)
        {
          return true;
        }
        else
        {
          delay(500);
        }         
      }
      else 
      {
        return false;
      }
    }
    else
    {
      return false;
    }
  }
  return false;
}

bool get_server_response()
{
  unsigned int response_timeout = 5000;
  unsigned long start_millis = millis();
  while ((millis() - start_millis) < response_timeout && !client.available()) {}
  if (client.available())
  {
    char response[256] = "";
    for (int i = 0; i < 256; i++)
    {
      if (!client.available()) break;
      response[i] = client.read();
    }
    char* good_response = strstr(response, "200 OK");
    if (good_response) return true;
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

void receive_data_event(int amount_received)
{
  while (Wire.available())
  {
    if (data_index >= DATA_SIZE) return; //Don't write if the buffer is full
    byte received_byte = Wire.read();
    data_array[data_index++] = received_byte;
  }
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

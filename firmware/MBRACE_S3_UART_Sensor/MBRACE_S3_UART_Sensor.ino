// === ESP32 UART Slave + Sensor + SD Card (FreeRTOS, PacketID, Resend Support) ===
//UART: 21,22
//SD_SPI: 5,18,19,23
//Sensor: 36,39,34,35,32,33,25,26,27,14,12,13,4,15
//SensorPower: 2 (with LED)  //test if in sleep will power auto down?  No.
//08112026: g_packetId didn't reset while timeout happen, 实际上因为没有清空queue导致之前的残留
//08112026: delete file no matter success or failed

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>
#include <FS.h>
#include <Wire.h> //01232026 手动关闭I2C

#define CMD_TME 0xA0
#define CMD_REQ 0xA1
#define CMD_RES 0xA2
#define CMD_ERR 0xA3
#define CMD_HED 0xA4
#define CMD_DAT 0xA5
#define CMD_RTY 0xA6  //retry
#define CMD_YES 0xA7
#define CMD_WAT 0xA8
#define CMD_EOF 0xA9


#define CS_PIN 5
#define RX_PIN 21 // UART  32, 33  ; 16, 17 //s3 use 8,9 cause trash bytes
#define TX_PIN 22
#define UART_FREQ 500000//2000000 
#define S3_TIMEOUT   300000    // CCHSEND 等待时间
// #define TEST_MODE

const int PayloadSize =115;// 240;
const int PayloadSize_cmd = 9;

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t id;
  uint8_t len;
  uint8_t payload[PayloadSize];
  uint16_t crc;
} DataPacket;

// DataPacket pkt;

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t id;
  uint8_t len;
  uint8_t payload_cmd[PayloadSize_cmd];
  uint16_t crc;
} CmdPacket;


const int DatPktBytes = sizeof(DataPacket);
const int CmdPktBytes = sizeof(CmdPacket);
const int CrcSize = 2;
const int Crc_Len = DatPktBytes - CrcSize;
const int Crc_Len_Cmd = CmdPktBytes - CrcSize;

QueueHandle_t packetQueue;

File g_dataFile;
File g_sendFile;//20260422
// TaskHandle_t xWatchdogTaskHandle = NULL;
bool Flag_xWatchdogTaskHandle = false;

uint8_t g_packetId = 0;  // Packet number
String g_currentFileName = "/data.csv";
// String HeadStr;
int filePointer = 0;
bool sendingtask = false;
volatile bool IsReadytosend = false;
bool headerSent = false;
unsigned long UartTimeout;

int errorCount=0;
long totalPackets = 0;
int IsUsingUart = -1;
DataPacket LastPacket;
DataPacket HeadPacket;
bool IsHeadReady = false;
// String datetime_rcv="0";
// unsigned long datetime_t = 0; //最大值是 4,294,967,295 毫秒，大约 49.71 天

#define SENSOR_COUNT 14
String sensorReadings[SENSOR_COUNT];

int sensorPinValues[SENSOR_COUNT] = {36,39,34,35,32,33,25,26,27,14,12,13,4,15};
int sensorPowerSwPin = 2;

bool RxPinHighDetected = false;

// CRC16
uint16_t crc16_update(uint16_t crc, uint8_t data) {
  crc ^= data;
  for (int i = 0; i < 8; ++i) {
    if (crc & 1) crc = (crc >> 1) ^ 0xA001;
    else crc = crc >> 1;
  }
  return crc;
}

uint16_t calculateCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc = crc16_update(crc, data[i]);
  }
  return crc;
}

void BuildPacket(DataPacket &packet, const uint8_t &cmd, const int &id, const uint8_t *body, const size_t &body_len) {
  packet.cmd = cmd;
  packet.id = id;
  packet.len = body_len;
  memcpy(packet.payload, body, body_len);
  packet.crc = calculateCRC((uint8_t*)&packet, Crc_Len);
}

void filePacketTask(void* pv) {
  // int queuecount;
  String headStr; 
  TaskHandle_t* handlePtr = (TaskHandle_t*)pv;
  
#ifdef TEST_MODE
    Serial.println("filePacketTask: begin run!"); 
#endif
  while(!IsReadytosend){
#ifdef TEST_MODE
    Serial.println("filePacketTask: not readytosend"); 
#endif
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  } // need mordify to xSemaphoreTake

#ifdef TEST_MODE
  Serial.println("filePacketTask: readytosend"); 
#endif

  g_sendFile = SD.open(g_currentFileName);
  if (!g_sendFile) { 
    sendingtask = false;
    Serial.println("Err_filePacketTask: fail to open sendFile");  //may need reply fail to master  
    headStr = g_currentFileName + ":err";
    HeadPacket.cmd = CMD_HED;
    HeadPacket.id = ++g_packetId; //1
    HeadPacket.len =headStr.length();
    memcpy(HeadPacket.payload, (uint8_t*)headStr.c_str(), headStr.length()); 
    HeadPacket.crc = calculateCRC((uint8_t*)&HeadPacket, Crc_Len);
    IsHeadReady = true;
    Serial.println("filePacketTask: errheader sent");  
    UartTimeout = millis() + 20000;// let it shutdown earlier
    vTaskDelay(20 / portTICK_PERIOD_MS);
    vTaskDelete(NULL);     //del the task and won't run lately code
  }


  // Send header
  headStr = g_currentFileName + ":" + String(g_sendFile.size());
  HeadPacket.cmd = CMD_HED;
  HeadPacket.id = ++g_packetId; //1
  HeadPacket.len =headStr.length();
  memcpy(HeadPacket.payload, (uint8_t*)headStr.c_str(), headStr.length()); 
  HeadPacket.crc = calculateCRC((uint8_t*)&HeadPacket, Crc_Len);
  IsHeadReady = true;
#ifdef TEST_MODE
    Serial.println("filePacketTask: headStr = " + headStr); 
    Serial.printf("filePacketTask: headStr.length() = %d\n",headStr.length()); 
    Serial.printf("filePacketTask: estimate pkg count = %d\n",long(g_sendFile.size()/PayloadSize)); 
#endif
  vTaskDelay(20 / portTICK_PERIOD_MS);


#ifdef TEST_MODE
  Serial.println("filePacketTask: Loading file..."); 
#endif

  // Send body packets
  while (g_sendFile.available()) {
    // queuecount = uxQueueMessagesWaiting(packetQueue);
    uint8_t body[PayloadSize] = {0};
    size_t len = g_sendFile.read(body, PayloadSize);
    DataPacket packet;
    BuildPacket(packet, CMD_DAT, ++g_packetId, body, len);  //start from 2
    xQueueSend(packetQueue, &packet, portMAX_DELAY);//满了会自动阻塞等待
  }
#ifdef TEST_MODE
  Serial.println("filePacketTask: file end"); 
#endif
  // Send EOF   
  DataPacket eof;
  BuildPacket(eof, CMD_EOF, ++g_packetId, NULL, 0);
  xQueueSend(packetQueue, &eof, portMAX_DELAY);
  Serial.println("filePacketTask: eof sent & remove file");
  g_sendFile.close();
  SD.remove(g_currentFileName);
  // sendingtask = false;
  vTaskDelete(NULL);     //del the task and won't run lately code
}



void onReceive(int len) {
  CmdPacket cmdpkt;

  if (len != CmdPktBytes) {
    Serial.printf("❌ len = %d    CmdPktBytes = %d\n", len,CmdPktBytes);
    Serial.println("❌ Wrong packet size");
    errorCount++;
    uint8_t rcv[len]={0};
    Serial1.readBytes(rcv, len);

#ifdef TEST_MODE
    Serial.println("debug rcv wrong size cmd:");
      // 打印索引行
    for (size_t j = 0; j < len; ++j) {
      Serial.printf("%3d ", j);
    }
    Serial.println();

    // 打印数据行
    for (size_t j = 0; j < len; ++j) {
      Serial.printf(" %02X ", rcv[j]);
    }
    Serial.println();
#endif
    
    ++errorCount;
    return;
  }

  Serial1.readBytes((char*)&cmdpkt, CmdPktBytes);
  totalPackets++;
  uint8_t* ptr = (uint8_t*)&cmdpkt;

#ifdef TEST_MODE  
  Serial.println("debug rcv cmd:");
    // 打印索引行
  for (size_t j = 0; j < CmdPktBytes; ++j) {
    Serial.printf("%3d ", j);
  }
  Serial.println();

  // 打印数据行
  for (size_t j = 0; j < CmdPktBytes; ++j) {
    Serial.printf(" %02X ", ptr[j]);
  }
  Serial.println();
#endif
    

//check CRC
  // size_t crc_len = CrcPosition;  // 不包含最后一个字节（通常用于存放 CRC 本身）
  uint16_t calcrc = calculateCRC(ptr, Crc_Len_Cmd);
  if (calcrc != cmdpkt.crc) { 
    ++errorCount;
    Serial.printf("calcrc = %04X\n", calcrc);
    Serial.printf("❌ CRC Error at Seq %d :\n", cmdpkt.id);


#ifdef TEST_MODE
    // 打印索引行
    for (size_t j = 0; j < CmdPktBytes; ++j) {
      Serial.printf("%3d ", j);
    }
    Serial.println();

    // 打印数据行
    for (size_t j = 0; j < CmdPktBytes; ++j) {
      Serial.printf(" %02X ", ptr[j]);
    }
    Serial.println();

    Serial.printf("crc = %04X\n", cmdpkt.crc);
#endif

  } 
  // else {
  //   Serial.printf("✅ Packet %d OK\n", pkt.id);
  // }

  
//   if (pkt.cmd == CMD_TME) {
//     datetime_t = millis();
//     // memcpy(datetime_rcv, pkt.payload.c_str(), pkt.len);
// #ifdef TEST_MODE
//     Serial.println("onRequest: Time alignment: " + datetime_rcv); 
// #endif
//     DataPacket reply = {};
//     reply.id = pkt.id;  //0
//     reply.cmd = CMD_YES;
//     reply.len = 0;
//     reply.crc = calculateCRC((uint8_t*)&reply, sizeof(reply)-CrcSize);

//     Serial1.write((uint8_t*)&reply, sizeof(reply));
//     // LastPacket = reply;
//     // sendingtask = false;
//     IsUsingUart = 0;
//     return;
//   } 

  if (!sendingtask && cmdpkt.cmd != CMD_REQ) {
#ifdef TEST_MODE
  Serial.println("onRequest: didn't receive sendingtask request"); 
#endif
    return;
  } 

  if (cmdpkt.cmd == CMD_REQ) {
    // sendingtask = true;
    // HeadStr = "";
    IsHeadReady = false;
    filePointer = 0;
    headerSent = false;
    g_packetId = 0;

    xTaskCreatePinnedToCore(filePacketTask, "filePacketTask", 4096, NULL, 1, NULL, 1);
#ifdef TEST_MODE
    Serial.println("start filePacketTask");
#endif

//reply preparing
    DataPacket reply = {};

    if (cmdpkt.id != 0){  // for test
      Serial.printf("Err_onReceive: Not at 0! cmdpkt.id = %d, g_packetId = %d\n",cmdpkt.id,g_packetId);
    }
    reply.id = cmdpkt.id;  //0
    reply.cmd = CMD_YES;
    reply.len = 0;
    reply.crc = calculateCRC((uint8_t*)&reply, Crc_Len);

    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    Serial1.write((uint8_t*)&reply, DatPktBytes);
    LastPacket = reply;


#ifdef TEST_MODE    
      uint8_t* ptr = (uint8_t*)&LastPacket;
      for (int j = 0; j < DatPktBytes; ++j) {
        Serial.printf("%3d ",j);
      }
      Serial.println("");

      for (int j = 0; j < DatPktBytes; ++j) {
        Serial.printf(" %02X ", ptr[j]);
      }
      Serial.println();
#endif


  } 
  else if (cmdpkt.cmd == CMD_HED) {
//send head
    DataPacket reply = {};
    if(!IsHeadReady){
      reply.cmd = CMD_WAT;
      reply.id = cmdpkt.id;
      reply.len = 0;
      reply.crc = calculateCRC((uint8_t*)&reply, Crc_Len);
      Serial1.write((uint8_t*)&reply, DatPktBytes);
      LastPacket = reply;

      if (cmdpkt.id != 1){// for test
        Serial.printf("Err_onReceive: Not at 1! cmdpkt.id = %d, g_packetId = %d\n",cmdpkt.id,g_packetId);
      }
    }
    else{
      // reply.cmd = CMD_HED;
      // reply.len = HeadStr.length();
      // memcpy(reply.payload, (uint8_t*)HeadStr.c_str(), HeadStr.length()); 
      Serial1.write((uint8_t*)&HeadPacket, DatPktBytes);
      LastPacket = HeadPacket;

      if (cmdpkt.id != HeadPacket.id || cmdpkt.id != 1){// for test
        Serial.printf("Err_onReceive: Not at 1! cmdpkt.id = %d, HeadPacket.id = %d\n",cmdpkt.id,HeadPacket.id);
      }
    }
#ifdef TEST_MODE    
    uint8_t* ptr = (uint8_t*)&LastPacket;
    for (int j = 0; j < DatPktBytes; ++j) {
      Serial.printf("%3d ",j);
    }
    Serial.println("");

    for (int j = 0; j < DatPktBytes; ++j) {
      Serial.printf(" %02X ", ptr[j]);
    }
    Serial.println();
#endif

  } 

  else if (cmdpkt.cmd == CMD_DAT) {
    int queuecount;
    DataPacket reply = {};

    queuecount=uxQueueMessagesWaiting(packetQueue);
    // Serial.printf("onRequest: queuecount=%d\n",queuecount); 
    if (queuecount > 0) {  //??
      if (xQueueReceive(packetQueue, &reply, 0) == pdTRUE) {

  // #ifdef TEST_MODE    
  //     uint8_t* ptr = (uint8_t*)&reply;
  //     for (int j = 0; j < DatPktBytes; ++j) {
  //       Serial.printf("%2d ",j);
  //     }
  //     Serial.println("");

  //     for (int j = 0; j < DatPktBytes; ++j) {
  //       Serial.printf("%02X ", ptr[j]);
  //     }
  //     Serial.println();
  // #endif

        Serial1.write((uint8_t*)&reply, DatPktBytes);
        LastPacket = reply;
      }
    } 
    else {
  #ifdef TEST_MODE
      Serial.println("Err_onReceive: no packet to send!"); 
  #endif
    }
    // Serial.println("onReceive: end"); 
  }

  else if (cmdpkt.cmd == CMD_RTY) {
    Serial.printf("resend Id = %d\n",cmdpkt.id);

  #ifdef TEST_MODE  
    int j;
    Serial.println("lastPacket = ");
    uint8_t* ptr = (uint8_t*)&LastPacket;
    for (j = 0; j < DatPktBytes; ++j) {
      Serial.printf("%3d ",j);
    }
    Serial.println("");

    for (j = 0; j < DatPktBytes; ++j) {
      Serial.printf(" %02X ", ptr[j]);
    }
    Serial.println();
  #endif

    // int id = cmd.substring(5).toInt();
    // Simplified resend logic: always resend lastPacket
    if (LastPacket.id == cmdpkt.id) {  //need to modify 
      Serial1.write((uint8_t*)&LastPacket, DatPktBytes);
    }
    else{
      Serial.printf("Err_onReceive: resend packet id not match! cmdpkt.id = %d, LastPacket.id = %d\n",cmdpkt.id,LastPacket.id);
    }
    return;
  // }

  }

  else if(cmdpkt.cmd == CMD_EOF){
    Serial.printf("received CMD_EOF, ready to sleep");
    DataPacket reply = {};
    reply.id = cmdpkt.id;  //0
    reply.cmd = CMD_YES;
    reply.len = 0;
    reply.crc = calculateCRC((uint8_t*)&reply, Crc_Len);

    Serial1.write((uint8_t*)&reply, DatPktBytes);
    LastPacket = reply;
    sendingtask = false;
    IsUsingUart = 0;
  }

  else{
    DataPacket reply = {};
    reply.cmd = CMD_ERR;
    reply.len = 0;
    reply.crc = calculateCRC((uint8_t*)&reply, Crc_Len);
    Serial1.write((uint8_t*)&reply, DatPktBytes);
    LastPacket = reply;
  }

#ifdef TEST_MODE
  if (totalPackets % 1000 == 0) {
    Serial.printf("📊 Total: %d, total Errors: %d (%.2f%%)\n", totalPackets, errorCount,
      100.0 * errorCount / totalPackets);
  }
#endif
}



void sensorTask(void* pv) {
  // int sensorworking = -1;
  // int linecount = 0; //042326
  while (true) {
    if (!sendingtask && !RxPinHighDetected) {
      IsReadytosend=false;
      if (!g_dataFile){
        g_dataFile = SD.open(g_currentFileName, FILE_APPEND);
      }

      if (g_dataFile) {

        unsigned long currentTime = millis();
        String line = String(currentTime);
        
        digitalWrite(sensorPowerSwPin, HIGH);
        vTaskDelay(5 / portTICK_PERIOD_MS); //delay(5);// wait waking up, at least 2ms that the init value get 4095 and can be changed
        // Sample all sensors
        for (int i = 0; i < SENSOR_COUNT; i++) {
            unsigned short value = analogRead(sensorPinValues[i]);
            line += "," + String(value);
        }
        line += "\n";
        g_dataFile.print(line);

        //2026/04/23: let it flush automatically to save tf card lifetime and battery power
        //++linecount;
        // if (linecount>=10){
        //   g_dataFile.flush(); // actually write into file. 20260422:del this to save power and let it automatically save? will cause any problem while sleep? 
        //   linecount = 0;
        // }
        

#ifdef TEST_MODE
        Serial.print(line);
       
        // if (++sensorworking==0){
        //   Serial.println("sensorTask working");
        // }
        // else{
        //   Serial.printf(".");
        //   if (sensorworking>100){
        //     sensorworking=-1;
        //   }
        // }  
#endif

      }
      else{
        Serial.println("Err_sensorTask: cannot open dataFile");
      }

      digitalWrite(sensorPowerSwPin, LOW);
      vTaskDelay(5 / portTICK_PERIOD_MS);//wait transmission and print completed 
      // vTaskDelay(5000 / portTICK_PERIOD_MS);

      if (RxPinHighDetected){
        vTaskDelay(988 / portTICK_PERIOD_MS);
      }
      else{
        // 定时 100ms 唤醒
        esp_sleep_enable_timer_wakeup(88190); // 100,000 微秒 = 100ms  
        //100000-12737=87263,
        //100000-11806=88194
        //...1s,
        //(1012.73684) 1000000-12737=987263
        //(1011.80575) 1000000-11806=988194

        esp_light_sleep_start();
      }

      // delay(1000);// wait waking up
      // 继续下一次循环

    }
    else{
      // sensorworking=-1;
      if (g_dataFile) {
        g_dataFile.flush();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        g_dataFile.close();
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
      IsReadytosend=true;
      vTaskDelay(10000 / portTICK_PERIOD_MS);//wait sendingtask finish
    }
  }
}




int sleepdelay = 3000;
int workdelay = 1;
// int freecount = 0;
// int maxfreecount = 1000;
bool sleepstate = false;

void WatchdogTask(void* pv) {
  unsigned long t0 = 0;
  int rcvlen;
  bool isFisrtDectectByte = true;
  
  while(true){
    if (!Flag_xWatchdogTaskHandle) {
      Serial1.end();
      // 恢复 GPIO 模式
      gpio_reset_pin((gpio_num_t)RX_PIN);
      gpio_reset_pin((gpio_num_t)TX_PIN);
      pinMode(RX_PIN, INPUT_PULLDOWN); 
      pinMode(TX_PIN, INPUT_PULLDOWN);
      // digitalWrite(TX_PIN, LOW);
      logMessage("✅ ESP32 UART Sleep");
      vTaskDelete(NULL); 
    }
    vTaskDelay(1);

    rcvlen = Serial1.available();
    // Serial.printf("rcvlen = %d\n", rcvlen);
    if (rcvlen >= CmdPktBytes) {
      // Serial.println("received");
      // 
      // if (sleepstate){
      //   sleepstate = false;
      //   Serial.println("uart wake up!");
      // }

      onReceive(rcvlen);
      isFisrtDectectByte = true;
    }
    else if(rcvlen!=0 && rcvlen < CmdPktBytes){
      if (isFisrtDectectByte){
        isFisrtDectectByte = false;
        // Serial.println("detected dirty bytes 1st time");
        t0 = millis();

        // Serial.printf("WatchdogTask: detect rcvlen = %d, wait 2 more seconds. t0 = %d \n",rcvlen, t0);
        while (Serial1.available() < CmdPktBytes && millis() - t0 < 2000) {
          vTaskDelay(1 / portTICK_PERIOD_MS);
        }
      } 
      else if(millis() - t0 > 3000){
        Serial.printf("WatchdogTask: timeout, t1 = %d, t0 = %d \n",millis(), t0);
        Serial.println("clean dirty bytes:");
        uint8_t rcv[rcvlen]={0};
        Serial1.readBytes(rcv, rcvlen);
        
          // 打印索引行
        for (size_t j = 0; j < rcvlen; ++j) {
          Serial.printf("%3d ", j);
        }
        Serial.println();

        // 打印数据行
        for (size_t j = 0; j < rcvlen; ++j) {
          Serial.printf(" %02X ", rcv[j]);
        }
        Serial.println();
        isFisrtDectectByte = true;
      }
      
    }

//无通信，超时自动恢复采集状态

    // else if(!sleepstate){
    //   if(millis() - t0 > sleepdelay){
    //     sleepstate = true;
    //     // freecount = 0;
    //     Serial.println("uart sleep!");
    //   }
    //   vTaskDelay(workdelay / portTICK_PERIOD_MS);
    // }
    // else{
    //   vTaskDelay(sleepdelay / portTICK_PERIOD_MS);
    // }
  }
}



void setup() {
  //01232026: 显式结束 I2C 总线，防止后台占用
  Wire.end(); 
  Serial.begin(115200);
  delay(3000);//延长初始化时间避免出现乱码

  if (!SD.begin(CS_PIN)) {
    Serial.println("SD init failed!");
    return;
  }

  pinMode(RX_PIN, INPUT_PULLDOWN); //INPUT
  pinMode(TX_PIN, INPUT_PULLDOWN);
  // digitalWrite(TX_PIN, LOW);
  delay(1000);
  // digitalWrite(TX_PIN, LOW);
  // delay(1000);
  // digitalWrite(TX_PIN, HIGH);

  pinMode(sensorPowerSwPin, OUTPUT);
  digitalWrite(sensorPowerSwPin, LOW);


  // analogReadResolution(12);
  for (int i = 0; i < SENSOR_COUNT; i++) {
    pinMode(sensorPinValues[i], INPUT);   //INPUT_PULLUP
    // analogSetPinAttenuation(sesnsorPinValues[i], ADC_11db);
  }

  // Serial1.begin(UART_FREQ, SERIAL_8N1, RX_PIN,TX_PIN);  

  packetQueue = xQueueCreate(10, DatPktBytes); //? 2?

  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 1, NULL, 1);
  Serial.println("✅ Slave ready (UART)");
}



void loop() {
  // Serial.println("✅ loop in");
  if (IsUsingUart==-1){
    unsigned long t0 = millis();

#ifdef TEST_MODE
    Serial.println("✅ loop IsUsingUart==-1");
#endif
    if (RxPinHighDetected == true){
      Serial.println("RX_PIN noticed Low!");
      RxPinHighDetected = false;
    }

    while (digitalRead(RX_PIN) == HIGH) {
#ifdef TEST_MODE
      Serial.println("RX_PIN noticed high!");
#endif
      RxPinHighDetected = true;

      if (millis() - t0 > 5000){ //800 times 10 for test
        // 收到 LilyGo 启动信号后回应1秒
        pinMode(TX_PIN, OUTPUT);
        digitalWrite(TX_PIN, HIGH);
        Serial.println("Reply TX_PIN turn high!");
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // 初始化 UART
        Serial1.begin(UART_FREQ, SERIAL_8N1, RX_PIN,TX_PIN);  
        Serial.printf("baud rate:%d\n",Serial1.baudRate());
        Flag_xWatchdogTaskHandle = true;
        // xTaskCreatePinnedToCore(WatchdogTask, "WatchdogTask", 4096, NULL, 1, &xWatchdogTaskHandle, 1);
        xTaskCreatePinnedToCore(WatchdogTask, "WatchdogTask", 4096, NULL, 1,NULL, 1);
        Serial.printf("WatchdogTask enable!\n");
        IsUsingUart = 1;
        sendingtask = true;
        UartTimeout = millis() + S3_TIMEOUT;
        Serial.println("✅ ESP32 UART Started");

        break;
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);//100 times 10 for test
    }

  }
  else if(IsUsingUart==0){
    // vTaskDelete(xWatchdogTaskHandle);  //04232026: may make E (30753) uart: uart_get_buffered_data_len(1706): uart driver error
    Flag_xWatchdogTaskHandle = false;
    vTaskDelay(100 / portTICK_PERIOD_MS);//wait until receiver got last pkg
    // Serial1.end();
    // // 恢复 GPIO 模式
    // gpio_reset_pin((gpio_num_t)RX_PIN);
    // gpio_reset_pin((gpio_num_t)TX_PIN);
    // pinMode(RX_PIN, INPUT_PULLDOWN); 
    // pinMode(TX_PIN, INPUT_PULLDOWN);
    // // digitalWrite(TX_PIN, LOW);
    IsUsingUart = -1;
    // Serial.println("✅ ESP32 UART Sleep");
    // Serial.println("✅ WatchdogTask deleted!");
  }
  else{// =1
      if (millis() > UartTimeout){
        Serial.println("Uart timeout! closing...");
        //04/22/2026: del data, let filePacketTask() del 
        g_sendFile.close();
        xQueueReset(packetQueue);
        sendingtask = false;
        IsUsingUart = 0;
        //03/26/2026: reset packet ID .. at CMD_REQ?
        // //08112026: delete file no matter success or failed
        // if (g_filePacketTaskHandle != NULL) {
        //   vTaskDelete(g_filePacketTaskHandle);
        //   g_filePacketTaskHandle = NULL;
        //   Serial.printf("loop:kill FilePacketTask and remove file!\n");
        //   // SD.remove(g_currentFileName);
        // }

        
      }
  }

  vTaskDelay(100 / portTICK_PERIOD_MS);// vTaskDelay(portMAX_DELAY);
  // Serial.println("✅ loop end");
}

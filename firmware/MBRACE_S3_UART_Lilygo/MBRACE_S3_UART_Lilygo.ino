/*
12/24/2025: modify file name format to data_dev2_20251011213000UTC.csv, add AWS upload function by directly sending to S3 url
01/22/2026: Remove GPS module and just use LTE to get time, Pin18 19 instead of Pin32 33
01/29/2026: Uart pin change to 32(TX)  34(RX), 18(TX) 39(RX). 21,22,23,19 are used by GPS. 19 need set as output low
01/30/2026: if too many tryreplytimes will sending cmd request, stop, close uart and go sleep. Otherwise, it will keep get bad bytes forever when the sensor ESP32 abnormal.
            Another known problem is when getting HEAD if keeping err it will keep asking HEAD forever, but it should be fine if connection is good. Therefore, keep the code right now.
02/07/2026: If packid mismatch, but CRC correct, directly return false.
02/17/2026: Move jsonbody parameter to global char in static to avoid Heap fragmentation
02/23/2026: Sometimes cannot receive whole presignedurl packet reply
02/25/2026: Delete String to prevent memory leak
02/27/2026: sdwritertask stuck writing to SD card. Put uploading task after all of devices finish copying.
03/10/2026: CCHSEND fail and Retry always fail. add initSSL()
03/10/2026: Add more log message write to TF card to catch UART slow/stop issue
03/10/2026: sync time everytime after uploading to keep no shift
03/10/2026: initSSL() add mode selection 0/1 
07/15/2026: SIM7670G
07/25/2026: SONDE
08/04/2026：Upload log to AWS
*/
#define TINY_GSM_RX_BUFFER 1024  // Set RX buffer to 1Kb
#define CODE_VER "20260810_1408"
#define BANNER_STR "=== MBrace Receiver Starting  v" CODE_VER " ==="

// #define TEST_MODE
// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS   //TEST


#define LILYGO_SIM7670G_S3_STAN
#define TINY_GSM_MODEM_SIM7670G
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <miniz.h>  // miniz: https://github.com/richgel999/miniz
#include "utilities.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <TinyGsmClient.h>
#include <SDI12.h>

// #include <TinyGPS++.h>
#include <time.h>
#include "driver/uart.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>  // 必须包含这个库来手动关闭它
#include "esp_err.h"
// #include <USB.h>  // ESP32-S3 USB CDC
#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

// #ifndef SerialGPS
// #define SerialGPS Serial2
// #endif

// // GPS pins for T-A7670 with GPS shield
// #define BOARD_GPS_TX_PIN                    21
// #define BOARD_GPS_RX_PIN                    22
// #define BOARD_GPS_PPS_PIN                   23
// #define BOARD_GPS_WAKEUP_PIN                19

// #define GPS_BAUD 9600
#define MODEM_ENABLE
// TinyGPSPlus gps;
TinyGsmClient client(modem);

#define CMD_REQ 0xA1
#define CMD_RES 0xA2
#define CMD_ERR 0xA3
#define CMD_HED 0xA4
#define CMD_DAT 0xA5
#define CMD_RTY 0xA6  //retry
#define CMD_YES 0xA7
#define CMD_WAT 0xA8
#define CMD_EOF 0xA9

#define RX1_PIN 47  // UART
#define TX1_PIN 14
#define RX2_PIN 37
#define TX2_PIN 21
#define UART_FREQ 500000

#define MODEM_BAUD 921600    //115200
#define CHUNK_SIZE 2048      // (1-2048) 2KB
#define BUF_SIZE 2048        //切换了部分 不懂哪里还有问题
#define SEND_TIMEOUT 20000   // CCHSEND 等待时间
#define MAX_RETRY 2          // 单 chunk 最大重发
#define S3_TIMEOUT 600000    // 整体等待时间  从 5分钟延长到10分钟 07/17/2026
#define S3_TIMEOUT_S 100000  // 短等待时间 for sonde

const char* API_HOST = "5pycakeikf.execute-api.us-east-1.amazonaws.com";
const char* API_PATH = "/dev/get-url";
const char* S3Url = "https://mbrace-data-csv-bucket.s3.amazonaws.com";
const char* S3Host = "mbrace-data-csv-bucket.s3.amazonaws.com";
// const char* S3Path = "/data";

static const int CCHMODEType = 0;



// 时钟监测变量
float measuredAPBFreq = 80;  // 默认80MHz
unsigned long lastClockCheck = 0;
const long CLOCK_CHECK_INTERVAL = 5000;  // 每5秒检查一次

const bool DEV_1 = true;  //both use Serial2
const bool DEV_2 = true;
const bool DEV_SONDE = false;
const bool DEV_LOG = true;
const int PayloadSize = 115;
const int PayloadSize_cmd = 9;
const int SleepDelay = 10000;
volatile bool isUploading_1_Finished = true;  // 初始为 false  07/18/2026： initial as true and change to false at the beginning of task. avoid wrong status
volatile bool isUploading_2_Finished = true;
volatile bool isUploading_Sonde_Finished = true;
volatile bool isUploading_Log_Finished = true;
volatile bool LTEStateIsOn = false;

// 02/17/2026: Move jsonbody parameter to global char in static to avoid Heap fragmentation
static char g_serialRxBuf[BUF_SIZE];
static int g_respLen = 0;

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t id;
  uint8_t len;
  uint8_t payload[PayloadSize];
  uint16_t crc;
} DataPacket;


typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t id;
  uint8_t len;
  uint8_t payload_cmd[PayloadSize_cmd];
  uint16_t crc;
} CmdPacket;

// DataPacket pkt;

// const int MaxSendBytes = sizeof(DataPacket);
const int CmdPktBytes = sizeof(CmdPacket);
const int DatPktBytes = sizeof(DataPacket);
const int CrcSize = 2;
const int Crc_Len = DatPktBytes - CrcSize;
const int Crc_Len_Cmd = CmdPktBytes - CrcSize;

const int UartTimeout = 100;     // milliseconds
const int flushInterval = 1000;  //100, flush every N packets   was 10

QueueHandle_t dataQueue;  // for communication between I2C and SD writer
// A queue that holds 10 memory addresses (pointers)
QueueHandle_t g_uploadQueue = xQueueCreate(10, sizeof(char*));

File dataFile;
// File targetFile;
bool writing = false;
bool writingFile = false;
// char filename[64];
bool nextflag = false;
bool receiving = false;
long bytesReceived;
String currentFileName = "data.csv";
int TotalErrCount = 0;
int maxreplyretrytimes = 10;
long TotalRcvdSize;


int errorCount = 0;
bool responseReceived = false;

#ifdef TEST_MODE
int Recv_count = 0;
#endif

int packetCounter = 0;
uint16_t crc = 0xFFFF;
uint8_t packetId = 0;  // Packet number

// File logFile;

// Log file name
const char* logFileName = "/rcv_Gulfport.log";

//03/04/2026:
String g_responseText;

#define MaxFilenameLength 64
struct FilePacket {
  char filename[MaxFilenameLength];  // using 32 + 1 (\0) chars right now. "/data_dev1_20250904172910UTC.csv"
  int size;
  bool end = false;
  uint8_t data[PayloadSize];
  int len;
};

//08/04/2026：Upload log to AWS
// ===== 上传周期配置 =====
enum IntervalMode {
  INTERVAL_2MIN_TEST,  // 测试用，固定2分钟（可选保留，方便调试）
  INTERVAL_HOURLY,
  INTERVAL_HALF_DAY,  // 每12小时上传一次
  INTERVAL_DAYLY      // 每24小时上传一次
};

uint64_t getIntervalSec(IntervalMode mode) {
  switch (mode) {
    case INTERVAL_2MIN_TEST: return 120;
    case INTERVAL_HOURLY: return 3600;
    case INTERVAL_HALF_DAY: return 43200;  // 12小时
    case INTERVAL_DAYLY: return 86400;     // 24小时
    default: return 3600;
  }
}

// ⚠️ 在这里切换你想要的上传频率
IntervalMode g_dat_Interval = INTERVAL_HOURLY;
IntervalMode g_log_Interval = INTERVAL_2MIN_TEST;


//07/25/2026: SONDE
#define SONDE_DATA_PIN 36  // ⚠️ 替换成你实测确认空闲、且已接好电平转换模块的GPIO
SDI12 sondeBus(SONDE_DATA_PIN);

const uint64_t SONDE_SAMPLE_INTERVAL_SEC = 900;  // 15分钟

const char* sondeAddress = "0";  // ⚠️ 请在KorEXO里核实实际地址
const char* sondeFileName = "/sonde_current.csv";
const char* sondeFieldNames = "Temp_C,DO_mgL,SpCond_uScm,pH";  // 按KorEXO实际勾选顺序调整

// 手动按字节读取，带超时和"空闲间隔"判断，比 readStringUntil 更可控、更方便调试
String readSDI12Raw(uint32_t timeoutMs, uint32_t idleGapMs = 200) {
  String resp = "";
  uint32_t lastByteTime = millis();
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (sondeBus.available()) {
      char c = sondeBus.read();
      resp += c;
      lastByteTime = millis();
    }
    if (resp.length() > 0 && (millis() - lastByteTime > idleGapMs)) {
      break;  // 连续idleGapMs没有新字节，认为这条响应结束
    }
  }
  return resp;
}


bool readSondeOnce(String& outLine) {
  sondeBus.begin();
  delay(100);  // 库文档建议begin后给一点稳定时间

  String cmd = String(sondeAddress) + "M!";
  sondeBus.sendCommand(cmd);

  String mResp = readSDI12Raw(2000);
  mResp.trim();

  Serial.print("[Sonde] M! raw: ");
  for (size_t i = 0; i < mResp.length(); i++) Serial.printf("%02X ", (uint8_t)mResp[i]);
  Serial.println();

  if (mResp.length() < 5) {
    Serial.println("Err_readSondeOnce: no reply to M!");
    sondeBus.end();
    return false;
  }

  // 标准格式 atttn: a=地址, ttt=等待秒数(3位), n=返回数据个数(1位)
  int waitSec = mResp.substring(1, 4).toInt();
  delay((waitSec > 0 ? waitSec : 1) * 1000UL + 300);

  String dcmd = String(sondeAddress) + "D0!";
  sondeBus.sendCommand(dcmd);

  String data = readSDI12Raw(2000);
  data.trim();

  Serial.print("[Sonde] D0! raw: ");
  for (size_t i = 0; i < data.length(); i++) Serial.printf("%02X ", (uint8_t)data[i]);
  Serial.println();

  sondeBus.end();

  if (data.length() < 3) {
    Serial.println("Err_readSondeOnce: no data from D0!");
    return false;
  }

  // 去掉开头地址位，把 +/- 分隔的数值转成逗号分隔
  String values = data.substring(1);
  values.replace("+", ",+");
  values.replace("-", ",-");
  if (values.startsWith(",")) values = values.substring(1);

  outLine = printLocalTime(1) + "," + values;
  return true;
}



void appendSondeReading() {
  String line;
  if (!readSondeOnce(line)) {
    logMessage("Err_appendSondeReading: sonde read failed");
    return;
  }

  bool isNewFile = !SD.exists(sondeFileName);
  File f = SD.open(sondeFileName, FILE_APPEND);
  if (!f) {
    logMessage("Err_appendSondeReading: cannot open sonde file");
    return;
  }
  if (isNewFile) {
    f.print("Timestamp,");
    f.println(sondeFieldNames);
  }
  f.println(line);
  f.close();

  Serial.print("Sonde reading appended: ");
  Serial.println(line);
}



void enqueueSondeFileForUpload() {
  if (!SD.exists(sondeFileName)) {
    Serial.println("No Sonde file");
    isUploading_Sonde_Finished = true;
    return;
  }

  String archivedName = "/sond_" + printLocalTime(0) + "U.csv";
  SD.rename(sondeFileName, archivedName.c_str());

  char* ptr_filename = strdup(archivedName.c_str());
  xQueueSend(g_uploadQueue, &ptr_filename, portMAX_DELAY);
  Serial.print("Sonde file enqueued for upload: ");
  Serial.println(archivedName);
  isUploading_Sonde_Finished = false;
}



void myWaitResponse(const int timeout) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    // 只要有数据就读，没数据立刻跳出，不等待回车
    while (SerialAT.available()) {
      char c = SerialAT.read();
#ifdef DUMP_AT_COMMANDS
      Serial.write(c);  // 直接字节输出，不会因为没有换行符而卡顿
#endif
    }
    yield();  // 释放 CPU 给 ESP32 的后台任务
  }
}


//03/10/2026: initSSL() add mode selection 0/1
bool initSSL(const int& mode) {
  myWaitResponse(100);
  if (mode == 0) {
    call_cchclosestop();
    modem.sendAT(GF("+CCHSET=1,0"));

    // A76XX Series_AT Command Manual_V2.01
    // Defined Values
    // <report_send_result> Whether to report result of CCHSEND, the default value is 0:
    // 0 No.
    // 1 Yes. Module will report +CCHSEND: <session_id>,<err> to MCU when complete sending data.
    // <recv_mode> The receiving mode, the default value is 0:
    // 0 Output the data to MCU whenever received data.
    // 1 Module caches the received data and notifies MCU with +CCHEVENT: <session_id>, RECV EVENT.
    // MCU can use AT+CCHRECV to receive the cached data (only in manual receiving mode).

    //CCHSET=1,0 : presigned url too long and will lose bytes
    //CCHSET=1,1 : connection close and cannot read

    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CCHSET Failed!");
      return false;
    }

    modem.sendAT(GF("+CCHMODE="), CCHMODEType);  //modem.sendAT("+CCHMODE=" + CCHMODEType);
    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CCHMODE Failed!");
      return false;
    }

    modem.sendAT(GF("+CCHSTART"));
    if (modem.waitResponse(SEND_TIMEOUT, "+CCHSTART: 0") != 1) {
      Serial.println("CCHSTART Failed!");
      return false;
    }

    modem.sendAT(GF("+CSSLCFG=\"sslversion\",0,4"));
    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CSSLCFG Failed!");
      return false;
    }

    modem.sendAT(GF("+CSSLCFG=\"authmode\",0,0"));
    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CSSLCFG Failed!");
      return false;
    }

  } else {

    modem.sendAT(GF("+CCHSTART"));
    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CCHSTART Failed!");
      return false;
    }

    modem.sendAT(GF("+CSSLCFG=\"sslversion\",0,4"));
    if (modem.waitResponse(SEND_TIMEOUT) != 1) {
      Serial.println("CSSLCFG Failed!");
      return false;
    }
  }




  return true;
}



bool openConnection(const char* host, int port) {
  //07152026  check if is clean before CCHOPEN
  int tmpval;
  modem.sendAT(GF("+CCHOPEN?"));
  tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCHOPEN: 0,\"\",,,"), GF("ERROR"), GF("OK"));
  Serial.print("CCHOPEN state : ");
  Serial.println(tmpval);
  Serial.print("Modem output: ");
  Serial.println(g_responseText);
  g_responseText = "";

  modem.sendAT(GF("+CCHOPEN=0,\""), host, GF("\","), port, GF(",2"));

  if (CCHMODEType == 0) {
    if (modem.waitResponse(SEND_TIMEOUT, "+CCHOPEN: 0,0") != 1) {
      Serial.println("CCHOPEN Failed!");
      return false;
    }

  } else if (CCHMODEType == 1) {
    // if (modem.waitResponse(SEND_TIMEOUT,"CONNECT 115200",) != 1) {//921600
    //   Serial.println("CCHOPEN Failed!");
    //   return false;

    unsigned long start = millis();
    String rescode = "FAIL";
    while (millis() - start < 5000) {  // 5s 超时
      if (SerialAT.available()) {
        String line = SerialAT.readStringUntil('\n');
        line.trim();
        Serial.println(line);  // 打印模块返回

        if (line.startsWith("CONNECT ")) {
          int firstComma = line.indexOf(' ');
          rescode = line.substring(firstComma + 1);
          break;
        }
      }
    }
    if (rescode == "FAIL") {
      Serial.println("CCHOPEN Failed!");
      return false;
    }
  } else {
    Serial.println("CCHOPEN CCHMODEType error!");
    return false;
  }

  return true;
}



bool sendHttpHeader(const String& method, const char* host, const char* path, const String& contenttype, size_t totalSize) {
  int tmpval;
  String header =
    method + " " + path + " HTTP/1.1\r\n"
                          "Host: "
    + host + "\r\n"
             "Content-Type: "
    + contenttype + "\r\n"
                    "Content-Length: "
    + String(totalSize) + "\r\n"
                          "Connection: close\r\n\r\n";

  size_t totalHeaderLen = header.length();
  size_t currentPos = 0;
  uint32_t start;

  myWaitResponse(200);
  Serial.printf("Ready to send Header, total length: %d Bytes\n", totalHeaderLen);
  //03/23/2026: Fail to send header sometimes, is because String header?
  //08102026 mark test code
  // Serial.println(header);

  while (currentPos < totalHeaderLen) {
    size_t remaining = totalHeaderLen - currentPos;
    size_t lenToSend = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

    String chunk = header.substring(currentPos, currentPos + lenToSend);

    modem.sendAT(GF("+CCHSEND=0,"), lenToSend);
    //03/04/2026:
    tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF(">"));  // Use int or int8_t
    if (tmpval != 1) {
      Serial.print("CCHSEND Failed in send http header! Received: ");
      Serial.println(tmpval);
      Serial.print("Modem output: ");
      Serial.println(g_responseText);
      // Clear it immediately after use to prepare for the next command
      g_responseText = "";
      return false;
    }
    // Clear it immediately after use to prepare for the next command
    g_responseText = "";


    // modem.streamWrite(chunk.c_str(), lenToSend);
    SerialAT.write((const uint8_t*)chunk.c_str(), lenToSend);



    // start = millis();
    // while (millis() - start < 5000) {
    //     while (SerialAT.available()) {
    //         uint8_t c = SerialAT.read();

    //         Serial.printf("%02X ", c);

    //         if (c >= 32 && c <= 126) {
    //             Serial.printf("'%c'\n", c);
    //         } else {
    //             Serial.println();
    //         }
    //     }
    // }

    //07152026 need to wait last CCHSEND get reply CCHSEND: 0,0
    // tmpval = modem.waitResponse(SEND_TIMEOUT,g_responseText, GF("+CCHSEND: 0,0"),GF("ERROR")); //,GF("OK")  CCHSET=1
    tmpval = modem.waitResponse(SEND_TIMEOUT, GF("+CCHSEND: 0,0"), GF("ERROR"));  //,  CCHSET=0

    // Serial.print("CCHSEND state : ");
    // Serial.println(tmpval);
    // Serial.print("Modem output: ");
    // Serial.println(g_responseText);
    // g_responseText = "";
    if (tmpval != 1) {
      Serial.println("chunk send failed");
      return false;
    }


    currentPos += lenToSend;

    // Serial.printf("After OK: %d\n",
    //           SerialAT.availableForWrite());
  }
  // Serial.println("DEBUG: streamWrite end...");
  // modem.sendAT("+CCHSEND?");
  // modem.waitResponse(3000, g_responseText);
  // Serial.println(g_responseText);
  // g_responseText = "";

  // Serial.printf("After streamWrite end: %d\n",
  //             SerialAT.availableForWrite());


  Serial.println("HTTP Header send finish");

  //071526 there may some dirty bit sent after streamwrite and need clear
  // delay(1000);
  // modem.waitResponse(1000);

  // modem.sendAT(GF("+CCHRECV?"));

  // start = millis();
  // while (millis() - start < 5000) {
  //     while (SerialAT.available()) {
  //         uint8_t c = SerialAT.read();

  //         Serial.printf("%02X ", c);

  //         if (c >= 32 && c <= 126) {
  //             Serial.printf("'%c'\n", c);
  //         } else {
  //             Serial.println();
  //         }
  //     }
  // }
  // Serial.printf("After first AT: %d\n",
  //             SerialAT.availableForWrite());

  // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
  //   Serial.println("AT Failed!");
  // }
  // // Serial.print("Modem output: ");
  // // Serial.println(g_responseText);
  // g_responseText = "";

  // Serial.printf("After first AT response: %d\n",
  //               SerialAT.availableForWrite());

  // modem.sendAT(GF(""));

  // Serial.printf("After 2nd AT: %d\n",
  //             SerialAT.availableForWrite());

  // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
  //   Serial.println("AT Failed pass!");
  // }
  // Serial.print("Modem output: ");
  // Serial.println(g_responseText);
  // g_responseText = "";

  // Serial.printf("After 2nd AT response: %d\n",
  //               SerialAT.availableForWrite());

  return true;
}



bool sendChunkWithRetry(const uint8_t* buf, size_t len) {
  for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {

    modem.sendAT(GF("+CCHSEND=0,"), len);
    // if (modem.waitResponse(SEND_TIMEOUT,GF(">")) != 1) {
    //   Serial.println("CCHSEND Failed!");
    //   continue;
    // }

    //03/04/2026:
    int tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF(">"));  // Use int or int8_t
    if (tmpval != 1) {
      Serial.print("CCHSEND Failed in send chunk! Received: ");
      Serial.println(tmpval);
      Serial.print("Modem output: ");
      Serial.println(g_responseText);
      Serial.print("attempt: ");
      Serial.println(attempt);
      // Clear it immediately after use to prepare for the next command
      g_responseText = "";
      continue;
    }
    // Clear it immediately after use to prepare for the next command
    g_responseText = "";

    // //07152026 TEST
    // Serial.println("DEBUG: streamWrite1 begin...");
    // Serial.printf("Before streamWrite1: %d\n",
    //             SerialAT.availableForWrite());

    // modem.sendAT("+CCHSEND?");
    // modem.waitResponse(3000, g_responseText);
    // Serial.println(g_responseText);
    // g_responseText = "";


    // modem.streamWrite((const char*)buf, len);
    SerialAT.write((const uint8_t*)buf, len);

    // //07152026 TEST
    // Serial.printf("After streamWrite1: %d\n",
    //           SerialAT.availableForWrite());


    if (CCHMODEType == 0) {
      int8_t res = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCHSEND: 0,0"), GF("ERROR"));  //,GF("OK")
      if (res == 1) {
        g_responseText = "";

        // //07152026 TEST
        // modem.sendAT("+CCHSEND?");
        // modem.waitResponse(3000, g_responseText);
        // Serial.println(g_responseText);
        // g_responseText = "";

        return true;  // SEND OK
      } else {
        Serial.printf("Chunk send failed, retry %d/%d\n",
                      attempt, MAX_RETRY);
        Serial.print("Received: ");
        Serial.println(res);
        Serial.print("Modem output: ");
        Serial.println(g_responseText);
        delay(200);
      }
      // Clear it immediately after use to prepare for the next command
      g_responseText = "";
    } else {
      return true;  // SEND OK
    }
  }
  return false;
}


static uint8_t g_uploadbuffer[CHUNK_SIZE + 1];
bool uploadFile(File& file, size_t totalSize) {
  int donesize = 0;
  int countrate = 0;
  unsigned long timetoprint = 0;

  // Serial.println("Uploading file...");
  logMessage("Uploading file...");
  // 整个文件上传开始时间
  unsigned long uploadStartMs = millis();


  while (file.available()) {
    size_t n = file.read(g_uploadbuffer, CHUNK_SIZE);
    g_uploadbuffer[n] = '\0';  //01232026 fix Garbled text
    if (!sendChunkWithRetry(g_uploadbuffer, n)) {
      Serial.println("Chunk permanently failed");
      return false;
    }

    // //07152026 TEST
    // Serial.printf("After uploadfile OK1: %d\n",
    //       SerialAT.availableForWrite());
    // Serial.println("DEBUG: streamWrite1 end...");

    //071526 there may some dirty bit sent after streamwrite and need clear
    // modem.sendAT(GF(""));

    // Serial.printf("After first AT1: %d\n",
    //             SerialAT.availableForWrite());

    // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
    //   Serial.println("AT1 Failed pass2!");
    // }
    // Serial.print("Modem output: ");
    // Serial.println(g_responseText);
    // g_responseText = "";

    // Serial.printf("After first AT1 response: %d\n",
    //               SerialAT.availableForWrite());

    // modem.sendAT(GF(""));

    // Serial.printf("After 2nd AT1: %d\n",
    //             SerialAT.availableForWrite());

    // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
    //   Serial.println("AT1 Failed!");
    // }
    // // Serial.print("Modem output: ");
    // // Serial.println(g_responseText);
    // g_responseText = "";

    // Serial.printf("After 2nd AT1 response: %d\n",
    //               SerialAT.availableForWrite());



    donesize += n;

    if (millis() > timetoprint) {
      if (int(donesize * 100 / int(totalSize)) > countrate) {
        countrate = int(donesize * 100 / int(totalSize));

        // %zu = size_t (totalSize)
        // %lu = unsigned long (donesize)
        // %d  = integer (countrate)
        // %%  = literal '%' sign
        //note: size limit for int!!!
        Serial.printf("totalSize = %zu done = %d rate = %d%%\n", totalSize, donesize, countrate);
      }
      timetoprint = millis() + 2000;
    }
  }

  logMessage("Uploading file finish");
  // 整个文件上传开始时间
  unsigned long uploadEndMs = millis();
  unsigned long uploadElapsedMs = uploadEndMs - uploadStartMs;
  if (uploadElapsedMs == 0) uploadElapsedMs = 1;
  float avgBytesPerSec = (donesize * 1000.0f) / uploadElapsedMs;
  float avgKBps = avgBytesPerSec / 1024.0f;
  float avgMbps = (donesize * 8.0f) / uploadElapsedMs / 1000.0f;

  // Serial.println("Upload file finished");

  char logBuf[128];
  snprintf(logBuf, sizeof(logBuf),
          "UPLOAD_SUMMARY bytes=%zu time_ms=%lu avg=%.2f KB/s (%.3f Mbps)",
          donesize, uploadElapsedMs, avgKBps, avgMbps);
  logMessage(logBuf);


  return true;
}



bool uploadData(const String& data, size_t totalSize) {
  size_t currentPos = 0;

  uint8_t buffer[CHUNK_SIZE];

  while (currentPos < totalSize) {
    size_t remaining = totalSize - currentPos;
    size_t n = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

    String chunk = data.substring(currentPos, currentPos + n);

    if (!sendChunkWithRetry((uint8_t*)chunk.c_str(), n)) {
      Serial.println("Chunk permanently failed");
      return false;
    }


    // //07152026 TEST
    // Serial.printf("After uploaddata OK1: %d\n",
    //       SerialAT.availableForWrite());
    // Serial.println("DEBUG: streamWrite1 end...");

    //071526 there may some dirty bit sent after streamwrite and need clear
    // modem.sendAT(GF(""));

    // Serial.printf("After first AT1: %d\n",
    //             SerialAT.availableForWrite());

    // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
    //   Serial.println("AT1 Failed pass!");
    // }
    // Serial.print("Modem output: ");
    // Serial.println(g_responseText);
    // g_responseText = "";

    // Serial.printf("After first AT1 response: %d\n",
    //               SerialAT.availableForWrite());

    // modem.sendAT(GF(""));

    // Serial.printf("After 2nd AT1: %d\n",
    //             SerialAT.availableForWrite());

    // if (modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("OK")) != 1) {
    //   Serial.println("AT1 Failed!");
    // }
    // // Serial.print("Modem output: ");
    // // Serial.println(g_responseText);
    // g_responseText = "";

    // Serial.printf("After 2nd AT1 response: %d\n",
    //               SerialAT.availableForWrite());




    currentPos += n;
    // Serial.printf("已发送: %d/%d 字节\n", currentPos, totalLength);
  }
  return true;
}



bool readHttpResult(char* jsonBody, int action, bool& peerClosedSeen) {
  //action = 0： no action;  1：get jsonBody
  unsigned long start = millis();
  bool res;
  char c;

  g_respLen = 0;
  memset(g_serialRxBuf, 0, sizeof(g_serialRxBuf));
  peerClosedSeen = false;  //7/27/2026 每次调用先重置调用方传进来的这个值

  while (millis() - start < 20000) {
    while (modem.stream.available()) {
      // resp += char(modem.stream.read());
      c = modem.stream.read();
      if (g_respLen < sizeof(g_serialRxBuf) - 1) {
        g_serialRxBuf[g_respLen++] = c;
      } else {
        Serial.println("Err_readHttpResult: Buff size not enough");
        break;
      }
    }
    //2/23/2026 Sometimes cannot receive whole packet reply
    if (action == 1) {
      if (strrchr(g_serialRxBuf, '}') != NULL && strstr(g_serialRxBuf, "\r\n\r\n") != NULL) {
        break;
      }
    } else {
      if (strstr(g_serialRxBuf, "HTTP") != NULL && strstr(g_serialRxBuf, "\r\n\r\n") != NULL) {
        break;
      }
    }
  }

  //08102026 mark test code
  Serial.println("HTTP RESPONSE:");
  Serial.println(g_serialRxBuf);

  //7/27/2026 在函数内部就把检测结果算好，赋值给调用方传进来的这个引用
  peerClosedSeen = (strstr(g_serialRxBuf, "+CCH_PEER_CLOSED") != NULL);

  if (action == 1) {
    // 1. 寻找 JSON 的起始大括号 {
    char* start = strchr(g_serialRxBuf, '{');

    // 2. 寻找 JSON 的结束大括号 }
    char* end = strrchr(g_serialRxBuf, '}');

    // 3. 安全检查：确保找到了大括号且顺序正确
    if (start && end && start < end) {

      size_t len = end - start + 1;
      if (len > 1600) {
        Serial.println("Err_readHttpResult: len > 1600");
      }
      memcpy(jsonBody, start, len);  // Fast, direct copy
      jsonBody[len] = '\0';          // Always remember to terminate!
    } else {
      Serial.println("Cannot find json body from reply");
      // return false;
    }
    // 重要：处理转义字符
    // Lambda 有时会把 & 变成 \u0026，需要换回来
    // jsonBody.replace("\\u0026", "&");
    //直接改lambda

    // Serial.println("jsonBody:");
    // Serial.println(jsonBody);
  }

  res = strstr(g_serialRxBuf, "HTTP/1.1 200") != NULL || strstr(g_serialRxBuf, "HTTP/1.1 204") != NULL;

  return res;
}



//02/17/2026: Move jsonbody parameter to global char in static to avoid Heap fragmentation
static StaticJsonDocument<BUF_SIZE> g_doc;  // 大小根据 body 长度调整

bool parsePresignedUrl(const char* httpBody, char* url) {
  // 使用 ArduinoJson 解析 JSON
  g_doc.clear();  // Important: clear static docs before reuse!
  DeserializationError error = deserializeJson(g_doc, httpBody);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  if (g_doc.containsKey("upload_url")) {
    // This avoids the 'String' heap fragmentation entirely
    const char* tempUrl = g_doc["upload_url"];
    if (tempUrl) {
      strlcpy(url, tempUrl, BUF_SIZE);
    }
  }

  return true;
}


bool call_cchclosestop() {
  int tmpval;
  bool state_CCHOPEN = false;
  // bool state_CCHSET = false;

  // modem.sendAT(GF("+CCHSET?"));   // not by this cmd to check ssl running or not. Can only by CCHSTART/CCHSTOP to check status. if fail then means at that status. Don't need to check SSL status. if SSL on, must have set CCHSET already
  // tmpval = modem.waitResponse(SEND_TIMEOUT,g_responseText, GF("+CCHSET: 1"), GF("ERROR"), GF("+CCHSET: 0"));
  // Serial.print("CCHSET state : ");
  // Serial.println(tmpval);
  // Serial.print("Modem output: ");
  // Serial.println(g_responseText);
  // g_responseText = "";
  // if (tmpval == 1) state_CCHSET = true;

  // //wait OK so that not effect later transaction
  // modem.waitResponse(500,g_responseText, GF("OK"));

  // if (state_CCHSET){
  modem.sendAT(GF("+CCHOPEN?"));
  tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCHOPEN: 0,\"\",,,"), GF("ERROR"), GF("OK"));
  Serial.print("CCHOPEN state : ");
  Serial.println(tmpval);
  Serial.print("Modem output: ");
  Serial.println(g_responseText);
  g_responseText = "";
  if (tmpval == 3) state_CCHOPEN = true;
  // }
  //wait OK so that not effect later transaction
  modem.waitResponse(500, GF("OK"));

  if (state_CCHOPEN) {
    modem.sendAT(GF("+CCHCLOSE=0"));
    tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCHCLOSE: 0,0"), GF("ERROR"));
    Serial.print("CCHCLOSE :");
    Serial.println(tmpval);
    Serial.print("Modem output: ");
    Serial.println(g_responseText);
    g_responseText = "";
    if (tmpval == 1) {
      // It said OK, but let's wait a tiny bit to see if an ERROR follows
      if (modem.waitResponse(500, g_responseText, GF("ERROR")) == 1) {
        // Caught the hidden error!
        Serial.println("Caught the hidden error!");
        Serial.print("Modem output: ");
        Serial.println(g_responseText);
      }
      g_responseText = "";
    }
  }

  // if (state_CCHSET){
  //   modem.sendAT(GF("+CCHSTOP"));
  //   tmpval = modem.waitResponse(SEND_TIMEOUT,g_responseText,GF("+CCHSTOP"));
  //   if (tmpval!= 1) {
  //     Serial.print("CCHSTOP Failed: ");
  //     Serial.println(tmpval);
  //   }
  //   Serial.print("Modem output: ");
  //   Serial.println(g_responseText);
  //   g_responseText = "";
  // }

  return true;
}


//02/17/2026: Move jsonbody parameter to global char in static to avoid Heap fragmentation
static char g_PresignedUrl[BUF_SIZE];
static char jsonresp[BUF_SIZE];

char* getPresignedUrl(const char* host, const char* path, const String& filename) {
  //08112026: dev1/...   log/...  sonde/...
  String foldername = filename.substring(1, 5) + "/" + filename.substring(5, 14);  //07152027  short to less than  64  . 1. UTC => U    2. data_dev2_20260316 (19)  - >  dev2_20260316  (14)
  //  "/data_dev1_20260123..."
  //   01234567890123456789
  //08102026:  log__20260810/...    sond_20260810/...  modify at the function which used to rename the file
  String body = "{\"filename\":\"" + foldername + filename + "\"}";
  size_t totalSize = body.length();
  bool res = true;
  int tmpval;
  bool peerClosed = false;

  memset(g_PresignedUrl, 0, sizeof(g_PresignedUrl));
  do {
    if (!openConnection(host, 443)) {
      Serial.println("Set Url failed");
      res = false;
      break;
    }

    if (!sendHttpHeader("POST", host, path, "application/json", totalSize)) {
      Serial.println("Send HttpHeader failed");
      res = false;
      break;
    }
    // delay(10); //07152026 CCHSEND=0,65 ERROR could be time gap to short？  need to wait last CCHSEND get reply CCHSEND: 0,0
    if (!uploadData(body, totalSize)) {
      Serial.println("uploadData failed");
      res = false;
      break;
    }

    memset(jsonresp, 0, sizeof(jsonresp));
    if (!readHttpResult(jsonresp, 1, peerClosed)) {
      Serial.println("Upload failed at HTTP level");
      res = false;
      break;
    } else {
      Serial.println("Upload success!");
      // res = true;
      break;
    }
  } while (0);

  if (!res) {
    call_cchclosestop();
    return g_PresignedUrl;
  }

  // strlcpy(g_PresignedUrl, parsePresignedUrl(jsonresp), sizeof(g_PresignedUrl));
  if (!parsePresignedUrl(jsonresp, g_PresignedUrl)) {
    Serial.println("Err_getPresignedUrl: fail to get PresignedUrl");
  }

  //08102026 mark test code
  // Serial.print("Parsed upload URL: ");
  // Serial.println(g_PresignedUrl);

  //7/27/2026
  if (peerClosed) {
    Serial.println("CCH_PEER_CLOSED already caught along with JSON response, skip waiting.");
  } else {
    //07152026 CCH_PEER_CLOSED: 0
    tmpval = modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCH_PEER_CLOSED: 0"), GF("ERROR"));
    Serial.print("CCH_PEER_CLOSED state : ");
    Serial.println(tmpval);
    Serial.print("Modem output: ");
    Serial.println(g_responseText);
    g_responseText = "";
  }

  delay(1000);
  return g_PresignedUrl;
}



bool uploadFileToS3(const char* host, const char* path, const String& filename) {

  File f = SD.open(filename.c_str(), FILE_READ);
  size_t totalSize = f.size();
  bool res = true;
  bool peerClosed = false;

  do {
    if (!openConnection(host, 443)) {
      Serial.println("Set S3 Url failed");
      res = false;
      break;
    }

    if (!sendHttpHeader("PUT", host, path, "text/csv", totalSize)) {
      Serial.println("Send HttpHeader failed");
      res = false;
      break;
    }

    if (!uploadFile(f, totalSize)) {
      Serial.println("uploadFile failed");
      res = false;
      break;
    }

    if (!readHttpResult(jsonresp, 0, peerClosed)) {
      Serial.println("Upload failed at HTTP level");
      res = false;
      break;
    } else {
      Serial.println("Upload success!");
      // res = true;
      break;
    }
  } while (0);

  f.close();

  if (peerClosed) {
    Serial.println("CCH_PEER_CLOSED already caught along with HTTP response, skip waiting.");
  } else {
    // modem.sendAT("+CCHCLOSE=0");
    if (modem.waitResponse(SEND_TIMEOUT, GF("+CCH_PEER_CLOSED")) != 1) {
      Serial.println("Error_in_uploadFileToS3: Cannot catch CCH_PEER_CLOSED!");
    }
  }
  myWaitResponse(500);  //01232026: try to clear other bytes and switch line
  modem.sendAT(GF("+CCHSTOP"));
  if (modem.waitResponse(SEND_TIMEOUT, GF("+CCHSTOP: 0")) != 1) {
    Serial.println("CCHSTOP Failed!");
  }
  return res;
}



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



bool UartCmdRequest(HardwareSerial& Serial_, const uint8_t& cmd, const uint8_t (&payload_cmd)[PayloadSize_cmd], DataPacket& reply, int tryreplytimes = 0, bool newpkg = false) {
  // uint8_t payload_cmd[PayloadSize_cmd]={0};

  CmdPacket req = {};
  req.cmd = cmd;
  req.id = packetId;  //0
  req.len = 0;
  req.crc = calculateCRC((uint8_t*)&req, Crc_Len_Cmd);
  Serial_.write((uint8_t*)&req, sizeof(req));

#ifdef TEST_MODE
  uint8_t* ptr = (uint8_t*)&req;
  // 打印索引行
  Serial.println("debug send cmd:");
  for (size_t j = 0; j < sizeof(req); ++j) {
    Serial.printf("%3d ", j);
  }
  Serial.println();

  // 打印数据行
  for (size_t j = 0; j < sizeof(req); ++j) {
    Serial.printf(" %02X ", ptr[j]);
  }
  Serial.println();

  Serial.printf("crc = %04X\n", req.crc);
#endif


//read reply
//get fake FF then true, always bypass the first packet (FF or next normal packet need to send)
// DataPacket reply;

//01/30/2026: if retry times > max retrytimes, need return false and go sleep
#ifdef TEST_MODE
  Serial.printf("tryreplytimes = %d \n", tryreplytimes);
#endif
  if (tryreplytimes >= maxreplyretrytimes) {
    return false;
  }

  //01/30/2026: quesiton: Does this most need while but not if???
  while (tryreplytimes < maxreplyretrytimes) {

    unsigned long t0 = millis();
    while (Serial_.available() < sizeof(DataPacket) && millis() - t0 < 1000) {
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    if (Serial_.available() != 0 && Serial_.available() < sizeof(DataPacket)) {
      Serial.printf("UartCmdRequest: detect serial_.available = %d, wait one more second. \n", Serial_.available());
      while (Serial_.available() < sizeof(DataPacket) && millis() - t0 < 2000) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
      }
    }
    if (Serial_.available() >= sizeof(DataPacket)) {
      reply = onReceive(Serial_, Serial_.available());
    } else {
      Serial.printf("UartCmdRequest: timeout, t1 = %d, t0 = %d \n", millis(), t0);
      Serial.printf("❌ No reply (timeout), req.id = %d\n", req.id);

      Serial.println("clean timeout bytes:");
      int rcvlen;
      rcvlen = Serial_.available();
      uint8_t rcv[rcvlen] = { 0 };

      Serial_.readBytes(rcv, rcvlen);

      // 打印索引行
      for (size_t j = 0; j < sizeof(rcv); ++j) {
        Serial.printf("%3d ", j);
      }
      Serial.println();

      // 打印数据行
      for (size_t j = 0; j < sizeof(rcv); ++j) {
        Serial.printf(" %02X ", rcv[j]);
      }
      Serial.println();

      errorCount++;
      return false;
    }



    // check CRC
    uint16_t crc_calc = calculateCRC((uint8_t*)&reply, Crc_Len);
    // Serial.printf("crc_calc = %04X\n",crc_calc);
    //02/07/2026: If packid mismatch, but CRC correct, directly return false.
    bool flag_packidmismatch = false;

    if (reply.id != packetId) {
      // Serial.println("Err_i2cRequest: packetId mismatch!");
      Serial.printf("Err_UartCmdRequest: packetId mismatch! reply.id = %d, packetId = %d\n", reply.id, packetId);
      flag_packidmismatch = true;
    }

    if (crc_calc != reply.crc) {
#ifdef TEST_MODE
      Serial.println("CRC mismatch!");
#endif
      // if CRC fail, request resend
      if (!UartCmdRequest(Serial_, CMD_RTY, payload_cmd, reply, ++tryreplytimes)) {  // need add packet id
        logMessage("Err_UartCmdRequest: send rty cmd request fail!");
        //01/30/2026: fail then need return false
        // break;
        return false;
      }
    } else {
      if (flag_packidmismatch) return false;
    }
    break;  //got true reply and ready to return
  }

  if (newpkg) ++packetId;
  return true;
}



DataPacket onReceive(HardwareSerial& Serial_, int len) {
  DataPacket rcvpkt;

  // Serial.println("OnDataRecv in");
  if (len != sizeof(DataPacket)) {
    Serial.printf("❌ len = %d    sizeof(DataPacket) = %d\n", len, sizeof(DataPacket));
    Serial.println("❌ Wrong packet size");
    errorCount++;
    uint8_t rcv[len] = { 0 };
    Serial_.readBytes(rcv, len);

#ifdef TEST_MODE
    // 打印索引行
    Serial.println("debug rcv err pkg:");
    for (size_t j = 0; j < sizeof(rcv); ++j) {
      Serial.printf("%3d ", j);
    }
    Serial.println();

    // 打印数据行
    for (size_t j = 0; j < sizeof(rcv); ++j) {
      Serial.printf(" %02X ", rcv[j]);
    }
    Serial.println();
#endif

  } else {
    Serial_.readBytes((char*)&rcvpkt, sizeof(DataPacket));


#ifdef TEST_MODE
    uint8_t* ptr = (uint8_t*)&rcvpkt;
    // 打印索引行
    Serial.println("debug rcv pkg:");
    for (size_t j = 0; j < sizeof(rcvpkt); ++j) {
      Serial.printf("%3d ", j);
    }
    Serial.println();

    // 打印数据行
    for (size_t j = 0; j < sizeof(rcvpkt); ++j) {
      Serial.printf(" %02X ", ptr[j]);
    }
    Serial.println();
#endif
  }
  return rcvpkt;
}



bool FilecopyTask(HardwareSerial& Serial_, TaskHandle_t& sdWriterHandle, int rx_pin, int tx_pin, const String& devNo) {
  DataPacket packet;
  uint8_t payload_cmd[PayloadSize_cmd] = { 0 };
  unsigned long t0;

  // #ifdef TEST_MODE
  //   Serial.printf("\n--- Requesting from slave 0x%02X ---\n", addr);
  // #endif
  packetId = 0;
  TotalErrCount = 0;  //? for what？if to many err stop sending?
  bool isSenderReady = false;

  pinMode(tx_pin, OUTPUT);
  digitalWrite(tx_pin, HIGH);
  logMessage("TX_PIN turns output and high!");
  t0 = millis();

  while (digitalRead(rx_pin) == LOW) {
    if (millis() - t0 > 20000) {  //2000 times 10 for test
      // timeout 2000ms
      logMessage("Err_FilecopyTask: Fail to wake slave up.");
      digitalWrite(tx_pin, LOW);  //avoid slave always detect high
      return false;               //01222026  return false to reset dev flag
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  logMessage("RX_PIN may noticed high! reset tx_pin!");
  //digitalWrite(tx_pin, LOW);  //avoid slave always detect high  //keep hign? any issue??  7/12/2026

  // Serial.printf("baud rate:%d\n",Serial_.baudRate());

  // 初始化 UART
  pinMode(rx_pin, INPUT);  // 取消下拉，避免 UART 空闲高时持续耗电
  //INPUT_PULLUP 也能正常工作，但只是增加一个弱上拉，通常没必要。只有对方 TX 在切换 UART 时可能短暂变成高阻，或者线路较容易受干扰，才考虑上拉

  Serial_.begin(UART_FREQ, SERIAL_8N1, rx_pin, tx_pin);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  CleanByteTask(Serial_);
  Serial.printf("baud rate:%d\n", Serial_.baudRate());
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  CleanByteTask(Serial_);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  CleanByteTask(Serial_);

  vTaskDelay(100 / portTICK_PERIOD_MS);

  if (!UartCmdRequest(Serial_, CMD_REQ, payload_cmd, packet, 0, true)) {
    logMessage("Err_FilecopyTask: Timeout or error at CMD_REQ");
    return false;
  }

  if (packet.cmd == CMD_YES) {
    logMessage("Sender has accepted to send file.");
  }

  t0 = millis();
  while (!isSenderReady) {
    if (millis() - t0 > 10000) {
      logMessage("Err_FilecopyTask: wait for HED timeout");
      return false;
    }

    if (!UartCmdRequest(Serial_, CMD_HED, payload_cmd, packet, 0, true)) {
      logMessage("Err_FilecopyTask: Timeout or error at CMD_HED");
      return false;
    }

    if (packet.cmd == CMD_HED) {
      isSenderReady = true;
      // packet_id = buffer[5]; .//need to check id ?
      // packet_len = packet[6];
      String file_info(String((char*)packet.payload, packet.len));
      int split1 = file_info.indexOf(':', 1);
      String fname = file_info.substring(0, split1);
      String fsize = file_info.substring(split1 + 1);

      FilePacket pkt;
      //07152027  short to less than  64
      split1 = fname.indexOf('.', 1);
      // currentFileName = fname.substring(0,split1) + "_dev" + devNo + "_" + printLocalTime(0) + "UTC." + fname.substring(split1 + 1);
      currentFileName = "/dev" + devNo + "_" + printLocalTime(0) + "U." + fname.substring(split1 + 1);
      strlcpy(pkt.filename, currentFileName.c_str(), MaxFilenameLength);
      // Serial.println("currentFileName = " + currentFileName);
      if (fsize == "err") {
        Serial.printf("Err_FilecopyTask: get err size from head, file may not able to read by sender.\n");
        return false;
      }

      if (sdWriterHandle != NULL) {
        vTaskDelete(sdWriterHandle);
        sdWriterHandle = NULL;
        Serial.printf("Err_FilecopyTask: SdWriterTask didn't close!\n");
      }
      StartSdWriterTask(sdWriterHandle);

      pkt.size = fsize.toInt();
      // pkt.end = false;
      pkt.len = 0;
      xQueueSend(dataQueue, &pkt, portMAX_DELAY);
      writingFile = true;
#ifdef TEST_MODE
      Serial.printf("File name = %s, size = %d\n", pkt.filename, pkt.size);
#endif
    } else if (packet.cmd == CMD_WAT) {
      logMessage("FilecopyTask: Wait until sender ready to send!");
      --packetId;
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    } else {
#ifdef TEST_MODE
      logMessage("Err_FilecopyTask: head start Without cmd <HEAD>!");
#endif
      TotalErrCount++;
      // i--;
      // continue; //back to send REQ again // or slaver ask master to send cmd again？ or depend on the cmd sent by slaver?
    }
  }


  bool done = false;
  bytesReceived = 0;

  Serial.println("FilecopyTask: begin to query file body!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  while (!done) {                                                           // use id to check packet sequence?
    if (!UartCmdRequest(Serial_, CMD_DAT, payload_cmd, packet, 0, true)) {  //
      logMessage("Err_FilecopyTask: Timeout or error during receiving data");
      return false;
    }

    FilePacket pkt;
    strlcpy(pkt.filename, currentFileName.c_str(), MaxFilenameLength);
    // Serial.println("currentFileName = " + currentFileName);
    if (packet.cmd == CMD_DAT) {
      pkt.len = packet.len;
      memcpy(pkt.data, packet.payload, pkt.len);
      xQueueSend(dataQueue, &pkt, portMAX_DELAY);
      bytesReceived += pkt.len;
#ifdef TEST_MODE
      Recv_count++;
      if (Recv_count % 100 == 0) {
        Serial.printf(".");
      } else if (Recv_count > 10000) {
        Recv_count = 0;
        Serial.printf("\n");
      }
#endif
    } else if (packet.cmd == CMD_EOF) {
#ifdef TEST_MODE
      Serial.println("EOF received");
#endif

      FilePacket endPkt;
      strlcpy(endPkt.filename, currentFileName.c_str(), MaxFilenameLength);
      endPkt.len = 0;
      endPkt.end = true;
      xQueueSend(dataQueue, &endPkt, portMAX_DELAY);

      if (!UartCmdRequest(Serial_, CMD_EOF, payload_cmd, packet, 0, true)) {
        logMessage("Warn_FilecopyTask: request CMD_EOF fail");
      }

      if (packet.cmd == CMD_YES) {
        logMessage("Sender has accepted finish sending file.");
      } else {
        logMessage("Warn_FilecopyTask: sender doesn't reply finish.");
      }

      done = true;
      // delay(500);
      // 04/01/2026: reduce power consumption
      Serial_.flush();
      Serial_.end();
      // gpio_reset_pin((gpio_num_t)rx_pin);
      // gpio_reset_pin((gpio_num_t)tx_pin);
      // pinMode(rx_pin, INPUT_PULLDOWN);
      // pinMode(tx_pin, OUTPUT);
      // digitalWrite(tx_pin, LOW);
      pinMode(rx_pin, INPUT_PULLDOWN);  //34,39 dont have internal pulldown res
      pinMode(tx_pin, INPUT_PULLDOWN);
    } else {
      Serial.printf("Err_FilecopyTask: Unknown CMD: %02X\n", packet.cmd);
      // Serial.printf("packet = %s \n", packet);  // this goes Guru Meditation Error and reset
      TotalErrCount++;
    }
  }


#ifdef TEST_MODE
  //check Stack
  // Printwatermark("FilecopyTask");
#endif

  // Wire.endTransmission();
  // }

  // #ifdef TEST_MODE
  logMessage("FilecopyTask: copy finish!");
  // delay(10000);//test
  // #endif

  return true;
}



void sdWriterTask(void* pvParameters) {
  FilePacket pkt;
  long fileSize = 1;
  //02/25/2026: Delete String to prevent memory leak
  char* ptr_filename;
  bool isDone = false;

  // 1. Cast the parameter back to a pointer-to-a-handle
  TaskHandle_t* handlePtr = (TaskHandle_t*)pvParameters;

  while (!isDone) {
    if (xQueueReceive(dataQueue, &pkt, portMAX_DELAY)) {

      if (dataFile) {
        // char fulldataFilePath[32];  // 最大 31 字符 + 结束符
        // snprintf(fulldataFilePath, sizeof(fulldataFilePath), "/%s", dataFile.name());
        if (strcmp(pkt.filename + 1, dataFile.name()) != 0) {  //bypass "/"
          Serial.printf("Err_sdWriterTask: dataFile.name() = %s.  pkt.filename = %s\n", dataFile.name(), pkt.filename);
          dataFile.close();
          dataFile = File();
          Serial.println("Err_sdWriterTask: didn't successfully close the last dataFile, closed for preparing next writing.");
        }
      }

      if (!dataFile) {  //new slaver device sending..
        dataFile = SD.open(pkt.filename, FILE_WRITE);
        // //07162026 test
        // if (!dataFile) {Serial.printf("[SD] Opened fail");}

        packetCounter = 0;
        TotalRcvdSize = 0;
        fileSize = pkt.size;
        // fileName = pkt.filename;//cause memory leak  64 bytes
        ptr_filename = strdup(pkt.filename);  //creates a completely new, independent copy of that text in a different part of the memory (the Heap). need release manually
        Serial.printf("[SD] Opened file: %s, size = %d\n", pkt.filename, fileSize);
      }

      if (dataFile && pkt.len > 0) {
        dataFile.write(pkt.data, pkt.len);
        TotalRcvdSize += pkt.len;
        packetCounter++;
        if (packetCounter >= flushInterval) {
          packetCounter = 0;
          //02/27/2026: sdwritertask stuck writing to SD card
          //there is only one physical pipeline to the SD card hardware
          // dataFile.flush();// flushing is dangerous. Every flush forces a FAT table update, which is the slowest part of SD communication. Only flush when pkt.end == true. Let the SD library manage the 512-byte buffer itself.
          // Serial.printf("[SD] File flushed: %s, size = %d\n", dataFile.name(), dataFile.size());
          Serial.printf("sdWriterTask: QueueMessagesWaiting = %d\n", uxQueueMessagesWaiting(dataQueue));
          if (fileSize > 2000000) {
            int tmprate = TotalRcvdSize / (fileSize / 100);  //cannot <100
            Serial.printf("sdWriterTask: TotalRcvdSize = %d -- total finished %d %%\n", TotalRcvdSize, tmprate);
          } else {
            Serial.printf("sdWriterTask: TotalRcvdSize = %d -- total finished %d %%\n", TotalRcvdSize, 100 * TotalRcvdSize / fileSize);
          }
        }
      }

      if (pkt.end) {
        if (dataFile) {
          dataFile.flush();
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          Serial.printf("[SD] File final flushed: %s, size = %d\n", dataFile.name(), dataFile.size());
          Serial.printf("sdWriterTask: QueueMessagesWaiting = %d\n", uxQueueMessagesWaiting(dataQueue));
          if (fileSize != int(dataFile.size())) {
            Serial.println("Err_sdWriterTask: [SD] File size not match!!!");
            Serial.printf("fileSize == %d, dataFile.size == %d\n", fileSize, int(dataFile.size()));
            //03/26/2026: retry flush.
            dataFile.flush();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            Serial.printf("[SD] File retry flushed: %s, size = %d\n", dataFile.name(), dataFile.size());
          } else {
            Serial.println("sdWriterTask: [SD] File size match! Close file.");
          }
          dataFile.close();
          dataFile = File();
        } else {
          Serial.println("Err_sdWriterTask: [SD] File not opening!");
        }
        // writingFile = false;
        packetCounter = 0;
        // StartUploadTask();
        isDone = true;
        // continue;
      }
    }
  }
  Serial.println("sdWriterTask: exit!");
  writingFile = false;  // To loop() and finally check finish writing then sleep

  //02/27/2026: sdwritertask stuck writing to SD card
  xQueueSend(g_uploadQueue, &ptr_filename, portMAX_DELAY);
  // //dev2 need wait dev1 end to avoid init modem twice
  //   if (strstr(ptr_filename, "dev2") != NULL) {//(fileName.indexOf("dev2") != -1) {
  //     unsigned long timeout = millis() + S3_TIMEOUT;
  //     while (!isUploading_1_Finished && millis() < timeout){
  //       Serial.println("dev2 is waiting dev1 uploading...");
  //       vTaskDelay(10000 / portTICK_PERIOD_MS);
  //     }
  //     if (!isUploading_1_Finished){
  //       Serial.println("dev1 uploading timeout (dev2 cancel uploading task)");
  //     }
  //     else{
  //       StartUploadTask((void*)ptr_filename);
  //     }
  //   }
  //   else{
  //     StartUploadTask((void*)ptr_filename);
  //   }


  // 返回该任务自启动以来，曾经剩余的最少栈空间（单位：字节/字，取决于平台）
  // 在 ESP32 Arduino 中，返回的是字节数 (Bytes)
  uint32_t remainingStack = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("Task [%s] Stack High Water Mark: %u\n", pcTaskGetName(NULL), remainingStack);

  size_t minInternalDRAM = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  Serial.printf("Absolute Min Free DRAM: %u bytes\n", minInternalDRAM);


  *handlePtr = NULL;
  vTaskDelete(NULL);  // 正确终止当前任务
}


TaskHandle_t sdWriterHandle_1 = NULL;
TaskHandle_t sdWriterHandle_2 = NULL;
TaskHandle_t g_UploadTaskHandle = NULL;

void StartSdWriterTask(TaskHandle_t& sdWriterHandle) {
  Serial.println("creating sdWriterTask");
  BaseType_t result = xTaskCreatePinnedToCore(
    sdWriterTask,
    "sdWriterTask",
    4096,
    (void*)&sdWriterHandle,  // <--- Pass the ADDRESS of the handle here
    1,
    &sdWriterHandle,
    1);

  if (result != pdPASS) {
    Serial.println("❌ Failed to create sdWriterTask");
  } else {
    Serial.println("Finish creating sdWriterTask");
  }
}



//02/27/2026: sdwritertask stuck writing to SD card
void StartFileUploadTask(TaskHandle_t& taskHandle) {
  BaseType_t result = xTaskCreatePinnedToCore(
    fileuploadTask,      // 任务函数名
    "fileuploadTask",    // 任务名称（调试用）
    4096,                // 堆栈大小（字）
    (void*)&taskHandle,  // 参数
    1,                   // 优先级
    &taskHandle,         // 任务句柄（不需要）
    1);

  if (result != pdPASS) {
    Serial.println("❌ Failed to create fileuploadTask");
  }
}



void fileuploadTask(void* pvParameters) {
  char* ptr_filename;
  TaskHandle_t* handlePtr = (TaskHandle_t*)pvParameters;

  if (!LTEStateIsOn) {
    initializeModem();
  }
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  Serial.println("[Upload] Sending data file via LTE...");
  // LTE upload logic here
  int retries = 2;
  char* ptr_presignedUrl = NULL;
  bool res = true;
  int offset = 0;

  while ((DEV_1 && !isUploading_1_Finished) || (DEV_2 && !isUploading_2_Finished) || (DEV_SONDE && !isUploading_Sonde_Finished) || (DEV_LOG && !isUploading_Log_Finished)) {
    if (xQueueReceive(g_uploadQueue, &ptr_filename, portMAX_DELAY)) {
      Serial.printf("Starting upload for: %s\n", ptr_filename);
      res = true;
      do {
        for (int i = 0; i < retries; i++) {
          delay(5000);
          if (!initSSL(0)) {
            Serial.println("Failed to init SSL setting.");
            res = false;
            continue;
          }

          for (int j = 0; j < retries; j++) {
            if (ptr_presignedUrl != NULL) {
              free(ptr_presignedUrl);  // 先放掉上一次的
            }
            ptr_presignedUrl = strdup(getPresignedUrl(API_HOST, API_PATH, ptr_filename));  // getPresignedUrl(API_HOST,API_PATH,ptr_filename);
            if (ptr_presignedUrl != NULL && ptr_presignedUrl[0] != '\0') break;
            Serial.print("Retry getting presigned URL...");
            Serial.println(j);
            //03/23/2026: close CCH, this retry will directly go to openconnection but doesn't check if still openning.
            call_cchclosestop();
            // myWaitResponse(1000); //delay(2000);

            //03/25/26: add cchstop
            Serial.println("Sending AT...");
            modem.sendAT(GF(""));
            if (modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("OK")) != 1) {
              Serial.println("AT Failed!");
            }
            Serial.print("Modem output: ");
            Serial.println(g_responseText);
            g_responseText = "";

            modem.sendAT(GF("+CCHSTOP"));
            if (modem.waitResponse(SEND_TIMEOUT, g_responseText, GF("+CCHSTOP: 0")) != 1) {
              Serial.println("CCHSTOP Failed!");
            }
            Serial.print("Modem output: ");
            Serial.println(g_responseText);
            g_responseText = "";
            myWaitResponse(500);

            if (!initSSL(0)) {
              Serial.println("Failed to init SSL setting.");
            }
          }

          if (ptr_presignedUrl == NULL || ptr_presignedUrl[0] == '\0') {  //if (presignedUrl == "") {
            Serial.println("Failed to get presigned URL after retries");
            res = false;
            continue;
          } else if (strncmp(ptr_presignedUrl, S3Url, strlen(S3Url)) == 0) {  //if (presignedUrl.startsWith(S3Url)){
            // presignedUrl = presignedUrl.substring(strlen(S3Url));
            offset = strlen(S3Url);
          } else {
            Serial.println("S3 Host not match with the presigned URL.");
            res = false;
            continue;
          }

          if (uploadFileToS3(S3Host, ptr_presignedUrl + offset, ptr_filename)) break;
          Serial.println("Retry uploading CSV...");
          call_cchclosestop();
        }
      } while (0);

      if (strstr(ptr_filename, "dev1") != NULL) {
        isUploading_1_Finished = true;  // 标记任务 1 完成
        Serial.println("dev 1 finished");
      } else if (strstr(ptr_filename, "dev2") != NULL) {
        isUploading_2_Finished = true;  // 标记任务 2 完成
        Serial.println("dev 2 finished");
      } else if (strstr(ptr_filename, "sonde") != NULL) {
        isUploading_Sonde_Finished = true;  // 标记任务 sonde 完成
        Serial.println("dev sonde finished");
      } else if (strstr(ptr_filename, "log") != NULL) {
        isUploading_Log_Finished = true;  // 标记任务 log 完成
        Serial.println("dev log finished");
      }



      Serial.println("Debug: free ptr_filename");
      if (ptr_filename != NULL) {
        free(ptr_filename);
        ptr_filename = NULL;  // This is the "flag" that it is gone
      }

      Serial.println("Debug: free ptr_presignedUrl");
      if (ptr_presignedUrl != NULL) {
        free(ptr_presignedUrl);
        ptr_presignedUrl = NULL;  // This is the "flag" that it is gone
      }


    } else {
      Serial.printf("Debug: error! This should not be printed\n");
    }
  }

  Serial.println("Debug: printing stack info:");

  // 返回该任务自启动以来，曾经剩余的最少栈空间（单位：字节/字，取决于平台）
  // 在 ESP32 Arduino 中，返回的是字节数 (Bytes)
  uint32_t remainingStack = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("Task [%s] Stack High Water Mark: %u\n", pcTaskGetName(NULL), remainingStack);

  size_t minInternalDRAM = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  Serial.printf("Absolute Min Free DRAM: %u bytes\n", minInternalDRAM);

  *handlePtr = NULL;
  vTaskDelete(NULL);  // 正确终止当前任务
}



void CleanByteTask(HardwareSerial& Serial_) {
  int rcvlen;
  unsigned long t0 = 0;

  t0 = millis();
  while (millis() - t0 < 1000) {
    rcvlen = Serial_.available();
    if (rcvlen != 0) {
      Serial.println("clean dirty bytes:");
      uint8_t rcv[rcvlen] = { 0 };
      Serial_.readBytes(rcv, rcvlen);

      // 打印索引行
      for (size_t j = 0; j < sizeof(rcv); ++j) {
        Serial.printf("%3d ", j);
      }
      Serial.println();

      // 打印数据行
      for (size_t j = 0; j < sizeof(rcv); ++j) {
        Serial.printf(" %02X ", rcv[j]);
      }
      Serial.println();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


void initializeSDCard() {
  Serial.println("Initializing SD Card...");

#ifdef BOARD_POWERON_PIN  //Not sure it will impact SD, but keep it
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  SPI.begin(BOARD_SCK_PIN, BOARD_MISO_PIN, BOARD_MOSI_PIN);
  if (!SD.begin(BOARD_SD_CS_PIN)) {
    Serial.println("SD Card Mount Failed!");
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");

  // Create log file with header if it doesn't exist
  if (!SD.exists(logFileName)) {
    File logFile = SD.open(logFileName, FILE_WRITE);
    if (logFile) {
      logFile.println("=== Cellular-SD Logger Started ===");
      // logFile.println("Timestamp,Latitude,Longitude,Date,Time,Address,Signal_Quality,AWS_Ping,Baidu_Ping,Google_Ping");
      logFile.close();
    }
  }

  Serial.println("SD Card initialized successfully");
}



void endSDCard() {
  SD.end();
  SPI.end();

  // #ifdef BOARD_POWERON_PIN//Not sure it will impact SD, but keep it
  //     pinMode(BOARD_POWERON_PIN, INPUT);
  // #endif
  pinMode(BOARD_SCK_PIN, INPUT);
  pinMode(BOARD_MISO_PIN, INPUT);
  pinMode(BOARD_MOSI_PIN, INPUT);
  pinMode(BOARD_SD_CS_PIN, INPUT);
}



// 2. 硬件复位函数
void hardwareResetModem() {
#ifdef MODEM_RESET_PIN
  Serial.println("reset Modem...");
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

#ifdef MODEM_FLIGHT_PIN
  pinMode(MODEM_FLIGHT_PIN, OUTPUT);
  digitalWrite(MODEM_FLIGHT_PIN, HIGH);
#endif

  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);

  // Turn on the modem
  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  Serial.println("Starting modem...");
  int retry = 0;
  while (!modem.testAT(1000)) {
    Serial.print(".");
    if (retry++ > 10) {
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      delay(100);
      digitalWrite(BOARD_PWRKEY_PIN, HIGH);
      delay(1000);
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      retry = 0;
    }
  }
  Serial.println();

  modem.sendAT(GF("E0"));  //TEST  E0 /E1  071526
  if (modem.waitResponse(SEND_TIMEOUT) != 1) {
    Serial.println("Error in initializeModem: ATE0 fail");
    // return false;
  }

  //turn off GPS for SIM7670G  7/12/2026
  modem.sendAT("+CGDRT=1,1");
  modem.waitResponse(1000);

  modem.sendAT("+CGSETV=1,0");
  modem.waitResponse(1000);

  modem.sendAT(GF("&W"));
  if (modem.waitResponse(SEND_TIMEOUT) != 1) {
    Serial.println("Error in initializeModem: AT&W fail");
    // return false;
  }

  // Check SIM card
  SimStatus sim = SIM_ERROR;
  while (sim != SIM_READY) {
    sim = modem.getSimStatus();
    switch (sim) {
      case SIM_READY:
        Serial.println("SIM card online");
        break;
      case SIM_LOCKED:
        Serial.println("SIM card is locked");
        break;
      default:
        break;
    }
    delay(1000);
  }
  myWaitResponse(5000);  //wait AT+SIMCOMATI

  // Get model info
  modem.sendAT(GF("+SIMCOMATI"));
  if (modem.waitResponse(SEND_TIMEOUT) != 1) {
    Serial.println("Error in initializeModem: Get SIMCOMATI fail");
    // return false;
  }

  modem.sendAT("+CSQ");
  if (modem.waitResponse(SEND_TIMEOUT, g_responseText) != 1) {
    logMessage("CSQ send failed");
  }
  logMessage("Modem output: ");
  logMessage(g_responseText);
  g_responseText = "";

  //     //SIM7672G Can't set network mode    07092026
  // #ifndef TINY_GSM_MODEM_SIM7672
  //   if (!modem.setNetworkMode(MODEM_NETWORK_AUTO)) {
  //     Serial.println("Set network mode failed!");
  //   }
  //   String mode = modem.getNetworkModes();
  //   Serial.print("Current network mode : ");
  //   Serial.println(mode);
  // #endif


#ifdef TINY_GSM_MODEM_HAS_NETWORK_MODE
  if (!modem.setNetworkMode(MODEM_NETWORK_AUTO)) {
    Serial.println("Set network mode failed!");
  }
  String mode = modem.getNetworkModeString();
  Serial.print("Current network mode : ");
  Serial.println(mode);
#endif

#ifdef TINY_GSM_MODEM_HAS_PREFERRED_MODE
  Serial.println("Set network preferred mode!");
  if (!modem.setPreferredMode(MODEM_PREFERRED_CATM_NBIOT)) {
    Serial.println("Set network preferred failed!");
  }
  String prefMode = modem.getPreferredModeString();
  Serial.print("Current preferred mode : ");
  Serial.println(prefMode);
#endif

#ifdef NETWORK_APN
  Serial.printf("Setting network APN: %s\n", NETWORK_APN);
  modem.sendAT(GF("+CGDCONT=1,\"IP\",\""), NETWORK_APN, "\"");
  if (modem.waitResponse(SEND_TIMEOUT) != 1) {
    Serial.println("Set network APN error!");
  }
#endif
}



void printTaskStats() {
  UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
  TaskStatus_t* pxTaskStatusArray = (TaskStatus_t*)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

  if (pxTaskStatusArray != NULL) {
    // 获取所有任务的状态
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, NULL);

    Serial.println("任务名称            状态  优先级  栈剩余(HWM)  任务编号");
    for (UBaseType_t i = 0; i < uxArraySize; i++) {
      Serial.printf("%-20s %-5u %-7u %-12u %u\n",
                pxTaskStatusArray[i].pcTaskName,
                pxTaskStatusArray[i].eCurrentState,
                pxTaskStatusArray[i].uxCurrentPriority,
                pxTaskStatusArray[i].usStackHighWaterMark,
                pxTaskStatusArray[i].xTaskNumber);
    }
    vPortFree(pxTaskStatusArray);  // 记得释放内存
  }
}



void checkmemory() {
  Serial.println("===============================================");
  Serial.print("uxTaskGetNumberOfTasks = ");
  Serial.println(uxTaskGetNumberOfTasks());

  printTaskStats();

  logMessage("Total free internal Heap: ");
  logMessage(String(ESP.getFreeHeap()));
  logMessage("Max Allocatable Block: ");
  logMessage(String(ESP.getMaxAllocHeap()));

  logMessage("Total free external Heap: ");
  logMessage(String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  logMessage("Max Allocatable Block: ");
  logMessage(String(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));

  // DRAM: 只有这种内存能给 Task 分配栈空间，且支持字节寻址 (8-bit)
  size_t dramFree = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  size_t dramMaxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

  // IRAM: 仅用于存储指令，不支持字节寻址，不能给 Task 用
  // 我们可以通过排除法查看只能 32-bit 对齐访问的内存（通常就是 IRAM）
  size_t totalInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t freeTotal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  size_t iramOnly = totalInternal - dramFree;

  Serial.printf("Total internal: %u\n", totalInternal);
  Serial.printf("Total internal block (include IRAM): %u\n", freeTotal);

  Serial.printf("DRAM (Usable for Tasks) - Total Free: %u, Max Block: %u\n", dramFree, dramMaxBlock);
  Serial.printf("IRAM (Instructions Only) - Estimated Free: %u\n", iramOnly);
  Serial.println("===============================================");
}



bool initializeModem() {
  Serial.println("Initializing Modem...");

  SerialAT.setRxBufferSize(4096);  // SIM7670G → ESP32 接收缓存
  SerialAT.setTxBufferSize(4096);  // ESP32 → SIM7670G 发送缓存
  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // delay(3000);

#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
  // digitalWrite(BOARD_POWERON_PIN, LOW);//test if only led or switch. no diff but keep high
#endif

  hardwareResetModem();

  // Wait for network registration
  Serial.println("Waiting for network registration...");
  unsigned long startMillis = millis();
  const unsigned long timeout = 60000;

  RegStatus status = REG_NO_RESULT;

  while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
    status = modem.getRegistrationStatus();
    switch (status) {
      case REG_UNREGISTERED:
      case REG_SEARCHING:
        Serial.print(".");
        delay(1000);
        break;
      case REG_DENIED:
        Serial.println("Network registration denied");
        return false;
      case REG_OK_HOME:
        Serial.println("Registration successful");
        break;
      case REG_OK_ROAMING:
        Serial.println("Registration successful (roaming)");
        break;
      default:
        delay(1000);
        break;
    }

    // 超时检查
    if (millis() - startMillis > timeout) {
      Serial.println("\nRegistration timeout! Resetting modem and ESP32...");
      hardwareResetModem();

      // initializeModem();
      // break;

      startMillis = millis();

      // delay(1000);
      // ESP.restart(); // 重启 ESP32 重新触发初始化流程
    }
  }

#ifdef MODEM_REG_SMS_ONLY
  while (status == REG_SMS_ONLY) {
    Serial.println("Registered for \"SMS only\", home network (applicable only when E-UTRAN), this type of registration cannot access the network. Please check the APN settings and ask the operator for the correct APN information and the balance and package of the SIM card. If you still cannot connect, please replace the SIM card and test again. Related ISSUE: https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/issues/307#issuecomment-3034800353");
    delay(5000);
    return false;
  }
#endif

  Serial.printf("Registration Status:%d\n", status);
  delay(1000);


  if (!modem.setNetworkActive()) {
    Serial.println("Failed to activate network");
  }

  delay(2000);
  String ipAddress = modem.getLocalIP();
  Serial.print("Network IP: ");
  Serial.println(ipAddress);
  LTEStateIsOn = true;

  modem.sendAT("+CSQ");
  if (modem.waitResponse(SEND_TIMEOUT, g_responseText) != 1) {
    logMessage("CSQ send failed");
  }
  logMessage("Modem output: ");
  logMessage(g_responseText);
  g_responseText = "";


  return true;
}



String getTimestamp() {
  return String(millis() / 1000) + "s";
}



// // 把 GPS 拿到的年月日时分秒写到 RTC
// void setESP32Time(int year, int month, int day, int hour, int min, int sec) {
//     struct tm t;
//     t.tm_year = year - 1900; // tm_year 是从 1900 开始
//     t.tm_mon  = month - 1;   // tm_mon 从 0 开始
//     t.tm_mday = day;
//     t.tm_hour = hour;
//     t.tm_min  = min;
//     t.tm_sec  = sec;

//     time_t epoch = mktime(&t);
//     struct timeval now = { .tv_sec = epoch };
//     settimeofday(&now, NULL);   // 设置 ESP32 系统时间
// }



String printLocalTime(const int& format) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Fail to get local time!");
    return "";
  }
  char buf[64];
  if (format == 0) {  //short
    // Serial.println(&timeinfo, "%Y%m%d%H%M%S");
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &timeinfo);
  } else {  //long
    // Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S ", &timeinfo);
  }
  String timeString = String(buf);
  return timeString;
}



/**
 * 初始化并同步系统时间
 * 优化点：使用 TinyGSM 接口，解析 15 分钟单位时区，同步 ESP32 RTC
 */
bool initializeTime() {
  // 1. 发送指令获取时钟
  modem.sendAT(GF("+CCLK?"));

  // 2. 等待响应头 +CCLK: "
  if (modem.waitResponse(GF("+CCLK: \"")) != 1) {
    Serial.println("Error in initializetime: Get CCLK fail");
    return false;
  }

  // 3. 读取内容直到引号结束 (例如: 26/01/22,14:11:10-24)
  String res = modem.stream.readStringUntil('\"');
  myWaitResponse(500);  // 清除后续的 OK

  // 4. 解析字段
  int year, month, day, hour, min, sec, tz;
  char sign;
  // 格式: yy/mm/dd,hh:mm:ss±zz
  if (sscanf(res.c_str(), "%d/%d/%d,%d:%d:%d%c%d", &year, &month, &day, &hour, &min, &sec, &sign, &tz) != 8) {
    Serial.println("Error in initializetime: date format error");
    return false;
  }

  // 5. 构建时间结构体 (tm_year 是从 1900 开始，tm_mon 是 0-11)
  struct tm t;
  t.tm_year = (year + 2000) - 1900;  //because it is 26, not 2026
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  t.tm_isdst = -1;

  time_t local_timestamp = mktime(&t);

  // 6. 计算时区偏移 (重要：tz 为 15 分钟单位，例如 24 表示 6 小时)
  long offset_seconds = tz * 15 * 60;
  if (sign == '-') offset_seconds = -offset_seconds;

  // 7. 同步到 ESP32 系统内核
  // 设置 Unix 时间戳 (UTC = 本地时间 - 偏移量)
  struct timeval tv = { .tv_sec = local_timestamp - offset_seconds };
  settimeofday(&tv, NULL);

  //Just need UTC time right now, skip setting up localtime
  // // 8. 设置系统时区环境 (以便以后直接使用 localtime() 获取本地时间)
  // // 此时区格式为: <名称><偏移小时> (注意 POSIX 格式中符号与常规相反)
  // char tzEnv[16];
  // snprintf(tzEnv, sizeof(tzEnv), "GMT%+d", (int)(offset_seconds / 3600) * -1);
  // setenv("TZ", tzEnv, 1);
  // tzset();

  Serial.printf("Time sync finish: 20%02d-%02d-%02d %02d:%02d:%02d (UTC%c%g)\n",
                year, month, day, hour, min, sec, sign, tz / 4.0);
  // myWaitResponse(1000); // 清除后续的 OK
  return true;
}



void logMessage(const String& message) {
  // Print to serial
  Serial.println(printLocalTime(1) + "LOG: " + message);

  // Write to SD card
  // //08/04/2026：Upload log to AWS

  // if (!SD.exists(logFileName)) {
  //   File logFile = SD.open(logFileName, FILE_WRITE);
  //   if (logFile) {
  //       logFile.println("=== Cellular-SD Logger Started ===");
  //       logFile.println(printLocalTime(1) + message);
  //       logFile.close();
  //   } else {
  //     Serial.println("Failed to write to SD card");
  //   }
  // }
  // else{

  File logFile = SD.open(logFileName, FILE_APPEND);
  if (logFile) {
    logFile.println(printLocalTime(1) + message);
    logFile.close();
  } else {
    Serial.println("Failed to write to SD card");
  }

  // }
}


//08/04/2026：Upload log to AWS
void enqueueLogFileForUpload() {
  if (!SD.exists(logFileName)) {
    Serial.println("No Log file");
    isUploading_Log_Finished = true;
    return;  // 这一轮还没产生日志文件，跳过
  }

  String archivedName = "/log__" + printLocalTime(0) + "U.log";

  // 关键：用rename而不是复制。rename后logFileName这个路径立刻"空出来"，
  // 后续任何logMessage()调用都会在logFileName下自动新建一个全新文件，
  // 不会碰到archivedName这个已经改名的文件，天然避免了"正在上传的同时又被写入"的冲突。
  bool ok = SD.rename(logFileName, archivedName.c_str());
  if (!ok) {
    Serial.println("Err_enqueueLogFileForUpload: rename failed, skip this cycle");
    return;
  }

  char* ptr_filename = strdup(archivedName.c_str());
  xQueueSend(g_uploadQueue, &ptr_filename, portMAX_DELAY);
  Serial.print("Log file enqueued for upload: ");
  Serial.println(archivedName);
  isUploading_Log_Finished = false;

  // 可选：归档后立刻预建新文件并写表头，保持每份日志格式一致
  File newLog = SD.open(logFileName, FILE_WRITE);
  if (newLog) {
    newLog.println("=== Cellular-SD Logger Started ===");
    newLog.close();
  } else {
    Serial.println("Failed to create new log file");
  }
}



void shutdownModem() {
  Serial.println("Shutdown Modem...");
  if (LTEStateIsOn == false) {
    Serial.println("LTEStateIsOn = false , skip shutdownModem funcion!");
    return;
  }

  modem.sendAT(GF("+NETCLOSE"));
  if (modem.waitResponse(SEND_TIMEOUT, g_responseText) != 1) {
    Serial.println("NETCLOSE send failed");
    //03/26/2026: after retry sending file , it seems will fail at netclose
    Serial.print("Modem output: ");
    Serial.println(g_responseText);
    // return false;
  }
  g_responseText = "";



  // 1. 尝试优雅的软关机 (Soft Power Off)
  // 这个指令会通知基站断开连接并保存闪存设置
  modem.sendAT(GF("+CPOF=1"));
  if (modem.waitResponse(SEND_TIMEOUT) != 1) {
    Serial.println("CPOF send failed");
    // return false;
  }
  myWaitResponse(1500);
  // 04/01/2026: reduce power consumption
  SerialAT.flush();
  myWaitResponse(100);
  SerialAT.end();
  Serial.println("Modem Closed");
  delay(1000);

  gpio_reset_pin((gpio_num_t)MODEM_RX_PIN);
  gpio_reset_pin((gpio_num_t)MODEM_TX_PIN);
  pinMode(MODEM_RX_PIN, INPUT);
  pinMode(MODEM_TX_PIN, INPUT);
  //04/01/2026: 07/29/2026 cannot shutdown modem even replied powerdown but led still on and seen to be restarted
  // #ifdef BOARD_POWERON_PIN
  //   digitalWrite(BOARD_POWERON_PIN, LOW);
  // #endif

  LTEStateIsOn = false;
}



void setup() {
  //01232026：显式结束 I2C 总线，防止后台占用
  Wire.end();
  // pinMode(19, OUTPUT);//turn GPS to standby mode
  // digitalWrite(19, LOW);
  // USB.begin();
  Serial.begin(115200);       //can be turned off at prod
  while (!Serial) delay(10);  // 等待串口连接
  delay(3000);                //延长初始化时间避免出现乱码

  Serial.println(BANNER_STR);  // 两处地方标记版本[1]

  if (DEV_1) {
    pinMode(RX1_PIN, INPUT_PULLDOWN);  //INPUT //34,39 dont have internal pulldown res
    pinMode(TX1_PIN, INPUT_PULLDOWN);
    // digitalWrite(TX1_PIN, LOW);
  }
  Serial.println("=== debug 1 ===");
  if (DEV_2) {
    pinMode(RX2_PIN, INPUT_PULLDOWN);  //INPUT //34,39 dont have internal pulldown res
    pinMode(TX2_PIN, INPUT_PULLDOWN);
    // digitalWrite(TX2_PIN, LOW);
  }
  Serial.println("=== debug 1.1 ===");
  // Initialize SD card first
  initializeSDCard();
  Serial.println("=== debug 2 ===");
  // // Initialize GPS
  // initializeGPS();

#ifdef MODEM_ENABLE
  // Initialize Modem
  initializeModem();
#endif

  initializeTime();


  // Log startup
  logMessage(BANNER_STR);
  logMessage("UTC: " + printLocalTime(0) + "UTC, System initialized successfully");  // 两处地方标记版本[2]

  // //shutdown GPS module & Modem
  // DisableGPS();

  // #ifdef MODEM_ENABLE
  //   DisableModem();
  // #endif


  //03/04/2026:
  g_responseText.reserve(64);  // Pre-allocate 64 bytes to stay safe

  dataQueue = xQueueCreate(50, sizeof(FilePacket));

  delay(1000);  // 给 Slave 启动时间
  Serial.println("✅ Master ready (UART)");
}


// 这个变量保存在 RTC 内存中，睡眠不会丢失
// This stores the timestamp in the RTC memory so it survives sleep
// RTC_DATA_ATTR uint64_t next_wakeup_time_sec = 0;
//07/25/2026: SONDE
RTC_DATA_ATTR uint64_t next_sonde_sample_sec = 0;  // sonde采样，独立15分钟，不受这次调整影响
RTC_DATA_ATTR uint64_t next_dat_upload_sec = 0;    // dev1/dev2/sonde 共用这一个
//08/04/2026：Upload log to AWS
RTC_DATA_ATTR uint64_t next_log_upload_sec = 0;

int g_transtimes = 0;

void loop() {
  unsigned long timeout;
  esp_err_t err;
  time_t now = time(NULL);

  logMessage("lilygo wake up");
  checkmemory();
  now = time(NULL);

  //07/25/2026: SONDE
  bool sondeDue = (next_sonde_sample_sec == 0) || (now >= (time_t)next_sonde_sample_sec);
  bool datUploadDue = (next_dat_upload_sec == 0) || (now >= (time_t)next_dat_upload_sec);
  bool logUploadDue = (next_log_upload_sec == 0) || (now >= (time_t)next_log_upload_sec);

  logMessage("Sonde sample due: " + String((long)((time_t)next_sonde_sample_sec - now)) + "sec");
  logMessage("Data upload due: " + String((long)((time_t)next_sonde_sample_sec - now)) + "sec");
  logMessage("Log upload due: " + String((long)((time_t)next_sonde_sample_sec - now)) + "sec");

  // sonde采样：不需要modem，独立执行，跟上传周期无关
  if (DEV_SONDE && sondeDue) {
    logMessage("Sonde sample due");
    appendSondeReading();
  }

  if (datUploadDue || logUploadDue) {
    Serial.println("[Command] Starting transfer...");

    if (datUploadDue) {
      if (DEV_1) {
        logMessage("[DEV_1] ...");
        isUploading_1_Finished = false;
        if (!FilecopyTask(Serial2, sdWriterHandle_1, RX1_PIN, TX1_PIN, "1")) {
          logMessage("Err_loop: DEV_1 FilecopyTask failed");

          if (Serial2) {
            // 04/01/2026: reduce power consumption
            Serial2.flush();
            Serial2.end();
          }
          // gpio_reset_pin((gpio_num_t)RX1_PIN);
          // gpio_reset_pin((gpio_num_t)TX1_PIN);
          pinMode(RX1_PIN, INPUT_PULLDOWN);
          pinMode(TX1_PIN, INPUT_PULLDOWN);
          isUploading_1_Finished = true;
        }
      }

      // checkmemory();

      // vTaskDelay(30000 / portTICK_PERIOD_MS);//test pin still get high signal after end and while connect dev2 serial

      if (DEV_2) {
        logMessage("[DEV_2] ...");
        isUploading_2_Finished = false;
        if (!FilecopyTask(Serial2, sdWriterHandle_2, RX2_PIN, TX2_PIN, "2")) {
          logMessage("Err_loop: DEV_2 FilecopyTask failed");
          if (Serial2) {
            // 04/01/2026: reduce power consumption
            Serial2.flush();
            Serial2.end();
          }
          // gpio_reset_pin((gpio_num_t)RX2_PIN);
          // gpio_reset_pin((gpio_num_t)TX2_PIN);
          pinMode(RX2_PIN, INPUT_PULLDOWN);  //34,39 dont have internal pulldown res
          pinMode(TX2_PIN, INPUT_PULLDOWN);
          isUploading_2_Finished = true;
        }
      }

      if (DEV_SONDE) {
        enqueueSondeFileForUpload();  // 把攒好的 sonde 数据也改名塞进这一轮上传..
      }
    }

    //08/04/2026：Upload log to AWS
    if (DEV_LOG && logUploadDue) {
      enqueueLogFileForUpload();
    }

    //02/27/2026: sdwritertask stuck writing to SD card
    StartFileUploadTask(g_UploadTaskHandle);


    if (datUploadDue) {
      if (DEV_1) {
        timeout = millis() + S3_TIMEOUT;
        while (!isUploading_1_Finished && millis() < timeout) {
          Serial.println("wait dev1 uploading...");
          vTaskDelay(10000 / portTICK_PERIOD_MS);
        }
        if (!isUploading_1_Finished) {
          Serial.println("dev1 uploading timeout");
        }
      }
      if (DEV_2) {
        timeout = millis() + S3_TIMEOUT;
        while (!isUploading_2_Finished && millis() < timeout) {
          Serial.println("wait dev2 uploading...");
          vTaskDelay(10000 / portTICK_PERIOD_MS);
        }
        if (!isUploading_2_Finished) {
          Serial.println("dev2 uploading timeout");
        }
      }
      if (DEV_SONDE) {
        timeout = millis() + S3_TIMEOUT_S;
        while (!isUploading_Sonde_Finished && millis() < timeout) {
          Serial.println("wait sonde uploading...");
          vTaskDelay(10000 / portTICK_PERIOD_MS);
        }
        if (!isUploading_Sonde_Finished) {
          Serial.println("sonde uploading timeout");
        }
      }
    }

    if (DEV_LOG && logUploadDue) {
      timeout = millis() + S3_TIMEOUT_S;
      while (!isUploading_Log_Finished && millis() < timeout) {
        Serial.println("wait log uploading...");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
      }
      if (!isUploading_Log_Finished) {
        Serial.println("log uploading timeout");
      }
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);

    if (g_UploadTaskHandle != NULL) {
      vTaskDelete(g_UploadTaskHandle);
      g_UploadTaskHandle = NULL;
      logMessage("Err_loop: g_UploadTaskHandle didn't close!");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);

    //03/10/2026: sync time everytime after uploading to keep no shift
    initializeTime();
    logMessage("UTC: " + printLocalTime(0) + "UTC, System sync time");

    shutdownModem();
  }

  // Set next wake up time
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    // If time isn't set yet, fallback to a safe fixed sleep
    if (++g_transtimes <= 2) {
      err = esp_sleep_enable_timer_wakeup(60000000);
      Serial.printf("enable wakeup err = %s (%d)\n",
                    esp_err_to_name(err), err);
    } else {
      err = esp_sleep_enable_timer_wakeup(3300000000);  // 60min
      Serial.printf("enable wakeup err = %s (%d)\n",
                    esp_err_to_name(err), err);
    }
  } else {
    now = time(NULL);  // Get current Unix timestamp in seconds
    uint64_t dat_interval;
    uint64_t log_interval = getIntervalSec(g_log_Interval);

    // 1. Determine the interval based on transtimes
    if (++g_transtimes <= 2) {
      dat_interval = 120;  // 2 minutes
      // 2. Initialize the target if it's the first run
      if (next_sonde_sample_sec == 0) {
        next_sonde_sample_sec = now + SONDE_SAMPLE_INTERVAL_SEC;
      }
      if (next_dat_upload_sec == 0) {
        next_dat_upload_sec = now + dat_interval;
      }
      if (next_log_upload_sec == 0) {
        next_log_upload_sec = now + log_interval;
      }
    } else {
      dat_interval = getIntervalSec(g_dat_Interval);
    }

    // 3. Calculate sleep time in seconds, then convert to microseconds
    //07/25/2026: SONDE 取3个锚点里最近的那个作为本次睡眠时长
    int64_t deltaSample = (int64_t)next_sonde_sample_sec - now;
    int64_t deltaData = (int64_t)next_dat_upload_sec - now;
    int64_t deltaLog = (int64_t)next_log_upload_sec - now;
    int64_t sleepDeltaSec = min(deltaSample, min(deltaData, deltaLog));
    if (sleepDeltaSec < 0) sleepDeltaSec = 0;

    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", sleepDeltaSec);
    logMessage(String("Sleep for: ") + buf + " seconds");

    // Set wakeup (Seconds * 1,000,000 = Microseconds)
    err = esp_sleep_enable_timer_wakeup((uint64_t)sleepDeltaSec * 1000000ULL);
    Serial.printf("enable wakeup err = %s (%d)\n", esp_err_to_name(err), err);

    // 4.检查哪些变量等于这个最小值 Update the anchor for the next cycle
    if (deltaSample == sleepDeltaSec) {
      next_sonde_sample_sec += SONDE_SAMPLE_INTERVAL_SEC;
    }
    if (deltaData == sleepDeltaSec) {
      next_dat_upload_sec += dat_interval;
    }
    if (deltaLog == sleepDeltaSec) {
      next_log_upload_sec += log_interval;
    }
  }

  checkmemory();

  //03/26/2026
  // SD.end();
  //04/01/2026: wake up then restart issue
  endSDCard();

  vTaskDelay(2000 / portTICK_PERIOD_MS);


  err = esp_light_sleep_start();
  Serial.printf("light sleep err = %s (%d)\n", esp_err_to_name(err), err);


  //03/31/2026: battery mode sometimes restart after waking up
  vTaskDelay(500 / portTICK_PERIOD_MS);

  // //07152026  test
  // USB.begin();
  // Serial.begin(115200);

  //03/26/2026: miss log after wake up
  // uint8_t cardType = SD.cardType();
  // if (cardType == CARD_NONE) {
  //     Serial.println("No SD card attached");
  //     // return;
  // }
  // else{
  //   logMessage("waked up and attached SD");
  // }
  initializeSDCard();

  // // Nothing here; all work done in RTOS tasks
  // vTaskDelay(portMAX_DELAY);
}

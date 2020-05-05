#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string.h>
#include <stdio.h>
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include <Arduino.h>
#include "esp32-hal-log.h"
#include <BLE2902.h>
#include <M5Stack.h>
#include <Wire.h> //I2C library

// SX1509 I2C I/O expander
#include <SparkFunSX1509.h>
const byte SX1509_ADDRESS = 0x3E;
SX1509 io;

#define DEBUG_EXEC
#define DEBUG_MSG
#define DEBUG_UI

int keypadPin = 36;
char keyOld;
unsigned long time_now, time_now10;

char batteryLevel;
volatile char updateBitfield;
bool sleepTimerExpired, updateLcd;

#define bitPwr (1 << 0)
#define bitVolume (1 << 1)
#define bitMute (1 << 2)
#define bitSrcAudio (1 << 3)
#define bitSrcVideo (1 << 4)

#include "knobSwitch.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

BLECharacteristic* sr5010_chars_ptr[SR5010_NUMCHARS];

char sr5010_searchByUUID(const char*);
char sr5010_searchByCmd(char*);
char sr5010_searchByArg(int cmdIndex, char* ptrArg);

char rxBuf[256], rxBufIdx, rxBufLen; //buffers are very large to handle strings from INET streams, etc.
char rxBuf1[256], rxBufIdx1, rxBufLen1; //buffers are very large to handle strings from INET streams, etc.

RTC_DATA_ATTR char UIStatePrevious;
RTC_DATA_ATTR char UIStateCurrent = 0;

bool deviceConnected = false;
bool oldDeviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;

      M5.Lcd.fillRect(0, M5.Lcd.height()-11, M5.Lcd.width()-1, 10, GREEN);
      M5.Lcd.setCursor(0, M5.Lcd.height()-11);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.print("Connected");
      M5.Lcd.setTextColor(WHITE);
      updateLcd = true;
      UIStateCurrent++;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;

      M5.Lcd.fillRect(0, M5.Lcd.height()-11, M5.Lcd.width()-1, 10, RED);
      M5.Lcd.setCursor(0, M5.Lcd.height()-11);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.print("Disconnected");
      M5.Lcd.setTextColor(WHITE);
      updateLcd = true;
      UIStateCurrent++;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {

      //INFO: https://github.com/espressif/arduino-esp32/issues/3153

      const char strUp[] = "UP";
      const char strDn[] = "DOWN";
      
      std::string val = pCharacteristic->getValue();
      std::string strUUID = pCharacteristic->getUUID().toString();

      Serial.printf("~> %s: %s\n", strUUID.c_str(), val.c_str());

      //fetch command string index using UUID
      char i = 0;
      const char* pStrOut;
      i = sr5010_searchByUUID(strUUID.c_str());
      //Serial.printf("%u\n",i);
      
      if(i != 0xFF)
      {
        int valNum = atoi(val.c_str()); // get argument index
        
        if(sr5010Map[i].attributes & BIT_ISUPDOWN)
        {
          if(valNum == 0) pStrOut = strUp;
          else pStrOut = strDn;
        }
        else
        {
          char j = 0;
          char argIndex = 0;
    
          //find argument string major index
          for(j = 0; j < i; j++)
          {
            argIndex += sr5010Map[j].numArgs;
          }
          argIndex += valNum; //add argument string minor index

          pStrOut = sr5010MapArgsOut[argIndex];
        }
        
        //write to serial port.
        #ifdef DEBUG_MSG
        Serial.printf("-> %s%s\n", sr5010Map[i].statusStr, pStrOut);
        #endif
        Serial2.printf("%s%s\r", sr5010Map[i].statusStr, pStrOut);
      }
    };
};

void printDeviceAddress() {
 
  const uint8_t* point = esp_bt_dev_get_address();
  Serial.print("MAC is ");
 
  for (int i = 0; i < 6; i++) {
 
    char str[3];
 
    sprintf(str, "%02X", (int)point[i]);
    Serial.print(str);
 
    if (i < 5){
      Serial.print(":");
    }
 
  }
  Serial.println("");
}

BLEServer *pServer;
BLEService *pService;

void setup() {
  //NOTE: must be anabled via "core debug level" Arduino menu.
  Serial.setDebugOutput(true);
  esp_log_level_set("*",ESP_LOG_VERBOSE);

  // initialize the M5Stack object
  //NOTE: Something in the M5Stack API makes BLE init unreliable?
  M5.begin(true, false, false, true);
  M5.Speaker.end();
  /*
    Power chip connected to gpio21, gpio22, I2C device
    Set battery charging voltage and current
    If used battery, please call this function in your project
  */
  M5.Power.begin();
  M5.Lcd.setBrightness(100);
  M5.Lcd.setTextWrap(true, true);
  
  //disable M5Stack speaker clicking noise.
  dacWrite(25,0);
  
  M5.Power.setWakeupButton(BUTTON_A_PIN);
  if(M5.Power.canControl())
  {
    batteryLevel = M5.Power.getBatteryLevel();
  }
  else
  {
    M5.Lcd.println("PMIC not found.");
  }

  // text print
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  #ifdef DEBUG_UI 
  Serial.println("Started LCD.");
  #endif
  //M5.Lcd.println("Started LCD.");
  //M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
  M5.update();
  
  Serial.begin(115200);
  #ifdef DEBUG_EXEC
  Serial.println("Starting BLE.");
  #endif

  Serial2.begin(9600, SERIAL_8N1, 16, 17); //pin 16=TXD, pin 17=RXD.
  
  #ifdef DEBUG_EXEC
  Serial.println("OK0");
  #endif 
  //INFO: https://github.com/nkolban/esp32-snippets/issues/144
  BLEDevice::init("KSCT");
  #ifdef DEBUG_EXEC
  Serial.println("OK1");
  #endif
  pServer = BLEDevice::createServer();
  #ifdef DEBUG_EXEC
  Serial.println("OK2");
  #endif 
  //INFO: https://github.com/nkolban/esp32-snippets/issues/114
  //INFO: https://github.com/espressif/esp-idf/issues/1087
  //INFO: https://esp32.com/viewtopic.php?t=7452
  //INFO: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/BLEServer.h#L67
  pService = pServer->createService(BLEUUID(SERVICE_UUID), (3*SR5010_NUMCHARS), 1);
  #ifdef DEBUG_EXEC
  Serial.println("OK3");
  #endif
  pServer->setCallbacks(new MyServerCallbacks());
  #ifdef DEBUG_EXEC
  Serial.println("OK4");
  #endif 
  
  BLECharacteristic *pCharacteristic;

  printDeviceAddress();
  
  #ifdef DEBUG_EXEC
  Serial.println("Creating characteristics:");
  #endif 
  int i;
  for(i = 0; i<SR5010_NUMCHARS; i++)
  {
    #ifdef DEBUG_EXEC
    Serial.println(sr5010Map[i].uuid);
    #endif 
    sr5010_chars_ptr[i] = pService->createCharacteristic(sr5010Map[i].uuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE \
    | BLECharacteristic::PROPERTY_NOTIFY); // | BLECharacteristic::PROPERTY_INDICATE);

    // Create a BLE Descriptor so notifications work.
    sr5010_chars_ptr[i]->addDescriptor(new BLE2902());
  
    sr5010_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }
  #ifdef DEBUG_EXEC
  Serial.printf("Created %u characteristics.\n", i);
  #endif 

  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  #ifdef DEBUG_EXEC
  Serial.println("Started BLE.");
  #endif 

  delay(500); //wait for power to settle, BLE packs a whollop.

  //read/refresh all statuses.
  for(i=0; i<SR5010_NUMCHARS; i++)
  {
    if(sr5010Map[i].numArgs > 0)
    {
      #ifdef DEBUG_EXEC
      Serial.printf("-> %s?\n", sr5010Map[i].statusStr);
      #endif 
      Serial2.printf("%s?\r", sr5010Map[i].statusStr);
      while(!receiveUpdateSerial());
      processUpdateSerial(rxBuf);
    }
  }

  if(io.begin(SX1509_ADDRESS))
  {
    //io.keypad(KEY_ROWS, KEY_COLS, sleepTime, scanTime, debounceTime);
    io.keypad(4, 4, 256, 32, 16);
    pinMode(36, INPUT_PULLUP); //SX1509 INT*
  }
  else Serial.println("SX1509 not found");

  time_now = millis();
  time_now10 = millis();
  sleepTimerExpired = false;
  updateLcd = true;
}

char keyNew = 0;
bool keyPressed = false;
char strTemp[16] = "";
bool keySuppress = false;
char i;
  
void loop() {

  if(millis() >= time_now + 100) //every 100ms
  {
    if(!digitalRead(35))
    {
      unsigned int keyData = io.readKeypad();
    
      if (keyData != 0) // If a key was pressed:
      {
        byte row = io.getRow(keyData);
        byte col = io.getCol(keyData);
        Serial.printf("Key (%d, %d)\n", row, col);

        keyData = (row*4) + col;
        
        switch(keyData)
        {
          case 0:
            sprintf(strTemp, "MNCLT");
          break;
    
          case 1:
            sprintf(strTemp, "MNCUP");
          break;
    
          case 2:
            sprintf(strTemp, "MNCRT");
          break;
    
          case 3:
            sprintf(strTemp, "MNCDN");
          break;
    
          case 4:
            sprintf(strTemp, "MNENT");
          break;
    
          case 5:
            sprintf(strTemp, "MNRTN");
          break;
    
          case 6:
            sprintf(strTemp, "MNMEN ON");
          break;
    
          case 7:
            sprintf(strTemp, "MNMEN OFF");
          break;
    
          default:
            strTemp[0] = 0;
          break;
        }

        if(strTemp[0])
        {
          Serial.printf("-> %s\n", strTemp);
          Serial2.printf("%s\r", strTemp);
        }
      }
    }

    time_now = millis();
  }

  // disconnecting
  if(!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      Serial.println("start advertising");
      oldDeviceConnected = deviceConnected;
  }
  
  // connecting
  if(deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  //connected
  if(deviceConnected) {
    
  }

  //process any updates coming from the serial port.
  if(receiveUpdateSerial()) 
  {
    processUpdateSerial(rxBuf);
    i++;
  }
  
  //process any updates coming from the console serial port.
  if(receiveUpdateSerial1()) 
  {
    //write to serial port.
    Serial.printf("-> %s\n", rxBuf1);
    Serial2.printf("%s\r", rxBuf1);
  }
  
  if(updateBitfield)
  {
    UIUpdate(updateBitfield);
    updateBitfield = 0;
    UIStateCurrent++;
  }
  
  if(UIStateCurrent != UIStatePrevious)
  {
    #ifdef DEBUG_UI
    Serial.printf("UIStateCurrent=%d\n", UIStateCurrent);
    #endif
    
    if(sleepTimerExpired)
    {
      //wake up LCD and process UI input.
      //M5.Lcd.wakeup();
      M5.Lcd.fillRect(0, M5.Lcd.height()-21, M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, M5.Lcd.height()-21);
      
      if(M5.Power.canControl())
      {
        batteryLevel = M5.Power.getBatteryLevel();
      }
      
      M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
      M5.Lcd.setBrightness(200);
      updateLcd = true;
      
      sleepTimerExpired = false;
    }
    
    time_now10 = millis();

    UIStatePrevious = UIStateCurrent;
  }
  
  if(updateLcd)
  {
    M5.update();
    updateLcd = false;
  }

  if(!sleepTimerExpired && (millis() - time_now10 >= 10000))
  {
    sleepTimerExpired = true;
    Serial.println("Sleeping now.");
    M5.Lcd.setBrightness(0);
    //M5.Lcd.sleep();
  }
}

int processUpdateSerial(char* strUpdate)
{
  Serial.printf("<- %s\n", strUpdate); //print incoming string to console.
  
  //isolate multiple updates within one response using <CR> token.
  int init_size = strlen(strUpdate);
  char delim[] = "\r";
  char lenCmd, lenArg;

  char *ptr = strtok(strUpdate, delim);
  int match;
  char i, j;

  while(ptr != NULL)
  {
    //parse update string and update corresponding characteristic value.
    char statusStr[256];

    i = sr5010_searchByCmd(ptr);

    if(i != 0xff)
    {
      lenCmd = strlen(sr5010Map[i].statusStr);
      lenArg = strlen(ptr) - lenCmd;
      
      memcpy(statusStr, &ptr[lenCmd], lenArg); //separate base command from argument.
      statusStr[lenArg] = '\0'; //terminate string.
      #ifdef DEBUG_MSG
      Serial.printf("[i] cmd: %s, len: %d, arg: %s, len: %d\n", ptr, lenCmd, statusStr, lenArg);
      #endif 

      //check if argument is numeric text. Store as string in characteristic value.
      if(isDigit(statusStr[0]))
      {
        sr5010_chars_ptr[i]->setValue(statusStr);
        match = 1;
      }
      else
      {
        //if argument is not numeric, compare against arg string list.
        j = sr5010_searchByArg(i, statusStr);
        if(j != 0xFF)
        {
          //if a match is found, insert the arg string index as characteristic value.
          sprintf(statusStr, "%d\n", j);
          sr5010_chars_ptr[i]->setValue(statusStr);
          match = 1;
        }
      }

      if(match)
      {
        #ifdef DEBUG_MSG
        Serial.printf("^ %s\n", sr5010_chars_ptr[i]->getValue().c_str());
        #endif
        sr5010_chars_ptr[i]->notify();
        //https://github.com/nkolban/ESP32_BLE_Arduino/blob/master/examples/BLE_notify/BLE_notify.ino
        delay(5);
        match = 0;
      }
      #ifdef DEBUG_MSG
      else Serial.println("\nNo Arg match.");
      #endif 

      switch(i)
      {
        case 0:
        case 2:
        case 3:
        case 4:
        case 5:
          //trigger UI update.
          UIStateCurrent++; //Just something to keep registering a difference.
          updateBitfield |= (1 << i);
          #ifdef DEBUG_UI
          Serial.printf("updateBitfield = 0x%X\n", updateBitfield);
          #endif
        break;

        default:
        break;
      }
    }
    #ifdef DEBUG_MSG
    else Serial.println("\nNo Cmd match.");
    #endif 

    ptr = strtok(NULL, delim); //parse next token.
  }

  return 0;
}

int receiveUpdateSerial(void)
{
  char rc;

  //receive any new status update.
  if(Serial2.available())
  {
    rxBufIdx = 0; rxBufLen = 0;
    
    //read in characters until we see a CR.
    while(Serial2.available())
    {
      rc = Serial2.read();
      if(rc != '\r') rxBuf[rxBufIdx++] = rc; 
      else 
      {
        rxBuf[rxBufIdx] = '\0'; rxBufLen = rxBufIdx;
        return rxBufLen;
      }
    }
  }
  
  return 0;
}

char sr5010_searchByCmd(char* ptrCmd)
{
  char retVal = 0xFF; //No match found.

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  int c = 0;
  int match = 0;
  char len = 0;

  //take length of each available command string.
  //compare over length of that string.
  
  while(i<SR5010_NUMCHARS && !match)
  {
    if(sr5010Map[i].numArgs > 0)
    {
      len = strlen(sr5010Map[i].statusStr);
      c = memcmp(ptrCmd, sr5010Map[i].statusStr, len);
    }
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else i++;
  }

  return retVal;
}

char sr5010_searchByArg(int cmdIndex, char* ptrArg)
{
  char retVal = 0xFF; //No match found.

  if(sr5010Map[cmdIndex].numArgs < 1) return retVal;

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  int j = 0; 
  int c = 0;
  int match = 0;
  char len = 0;

  //take length of each available command string.
  //compare over length of that string.

  #ifdef DEBUG_MSG
  Serial.printf("ptrArg: %s\nArgs: ", ptrArg);
  #endif

  for(i=0; i<cmdIndex; i++)
  {
    //skip to args for incoming command.
    j += sr5010Map[i].numArgs;
  }

  i = 0;  
  do
  {
    len = strlen(sr5010MapArgsOut[i+j]);
    #ifdef DEBUG_MSG
    Serial.printf("%s | ", sr5010MapArgsOut[i+j]);
    #endif
    
    c = memcmp(ptrArg, sr5010MapArgsOut[i+j], len);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }

    i++;
    
  } while(i < sr5010Map[cmdIndex].numArgs && !match);

  return retVal;
}

char sr5010_searchByUUID(const char* ptrUUID)
{
  char retVal = 0xFF; //No match found.

  //search for the command the written characteristic maps to.
  int i = 0;
  int c = 0;
  int match = 0;
  while(i<SR5010_NUMCHARS && !match)
  {
    c = strcmp(ptrUUID, sr5010Map[i].uuid);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else i++;
  }

  return retVal;
}

void UIUpdate(char bitField)
{
  char field, j, argIndex;
  bool refreshNeeded = false;
  #define NUM_FIELDS 5
  char cmdIndices[NUM_FIELDS] = {0, 2, 3, 4, 5};
  
  for(field = 0; field < NUM_FIELDS; field++)
  {
    if(bitField & (1 << field))
    {
      refreshNeeded = true;

      argIndex = 0;
      if(cmdIndices[field] != 2) //Skip volume field, volume is stored as an integer.
      {
        //find argument string major index
        for(j = 0; j < cmdIndices[field]; j++)
        {
          argIndex += sr5010Map[j].numArgs;
        }
        argIndex += atoi(sr5010_chars_ptr[cmdIndices[field]]->getValue().c_str());
        
        #ifdef DEBUG_UI
        Serial.printf("argIndex=%d\n", argIndex);
        #endif
      }

      M5.Lcd.fillRect(0, (10*field), M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, (10*field));
      
      switch(field)
      {
        case 0: //Power
          M5.Lcd.printf("Power: %s", sr5010MapArgsOut[argIndex]);
        break;

        case 1: //Volume (stored as integer, not index)
          M5.Lcd.printf("Volume: %s",sr5010_chars_ptr[cmdIndices[field]]->getValue().c_str());
        break;

        case 2: //Mute
          M5.Lcd.printf("Mute: %s", sr5010MapArgsOut[argIndex]);
        break;

        case 3: //Audio source
          M5.Lcd.printf("Audio: %s", sr5010MapArgsOut[argIndex]);
        break; 

        case 4: //Video source
          M5.Lcd.printf("Video: %s", sr5010MapArgsOut[argIndex]);
        break;

        /*
        case 7: //Zone 2
          M5.Lcd.printf("Zone2: %s", sr5010MapArgsOut[argIndex]);
        break;
        */

        default:
        break;
      }
    }
  }

  if(refreshNeeded) 
  {
    updateLcd = true;
  }
}

int receiveUpdateSerial1(void)
{
  char rc;

  //receive any new status update.
  if(Serial.available())
  {
    rxBufIdx1 = 0; rxBufLen1 = 0;
    
    //read in characters until we see a CR.
    while(Serial.available())
    {
      rc = Serial.read();
      if(rc != '\r') rxBuf1[rxBufIdx1++] = rc; 
      else 
      {
        rxBuf1[rxBufIdx1] = '\0'; rxBufLen1 = rxBufIdx1;
        return rxBufLen1;
      }
    }
  }
  
  return 0;
}

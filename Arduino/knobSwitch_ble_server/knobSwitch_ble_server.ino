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

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "18550d7d-d1aa-4968-a563-e8ebeb4840ea"

//NOTE: The STANDBY MODE must be set to NORMAL 
//instead of ECONOMY for the power function to work correctly.

#define BIT_ISUPDOWN 0b1

struct sr5010_property_map {
  char uuid[37];
  char statusStr[9]; //up to 8-character command with NULL terminator.
  int numArgs; //number of static string arguments
  char attributes; //bitfield
};

//we want to keep this info in flash, not in RAM.
const sr5010_property_map sr5010Map[] = {
  {"bb0dae34-05cb-4290-a8c4-6dad813e82e2", "PW", 2, 0},
  {"3ac1dc48-4824-4e03-8f5e-9b4a591a94e9", "ZM", 2, 0},
  {"88e1ef43-9804-411d-a1f9-f6cdbda9a9a9", "MV", 2, 0},
  {"60ef9a3d-5a67-464a-b7bf-fe1c187828f1", "MU", 2, 0},
  {"c21d2540-fe05-43d7-b929-54b76140e030", "SI", 13, 0},
  {"a6b89a0b-c740-4b82-bba5-df717b4163d5", "SV", 10, 0},
  {"0969d2b6-60d8-4a1b-a88c-8457449a453e", "TFHD", 0, 0},
  {"e377cf47-0f23-4004-ba4d-1f93c0193fca", "TFAN", 0, 0},
  {"153acaae-9181-48c5-908c-21e81d3a8f85", "Z2", 2, 0},
  //==== Increment-only characteristics
  {"edfc239e-ea86-472b-8d95-0bd9d613b56c", "MV", 0, BIT_ISUPDOWN}, //MV up/down
  {"2478b23b-6464-45dd-a0c5-9eb4923fd4aa", "Z2", 0, BIT_ISUPDOWN}, //Z2 up/down
};

//{outgoing argument strings}
const char sr5010MapArgsOut[][10] = {
  "ON", "STANDBY",
  "ON", "OFF", //ZM
  "UP", "DOWN",
  "ON", "OFF",
  //NOTE: Net/online music/usb/ipod/bluetooth functions return large strings that are custom-terminated.
    //This is stupid, so we skip them for now.
  "CD", "DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "TUNER", /*"IRADIO", "SERVER", "FAVORITES",*/ "AUX1", "AUX2", "INET", "BT", "USB/IPOD", 
  "DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "AUX1", "AUX2", "CD", "OFF",
  "ON", "OFF" //Z2
};

/*
//{argument's associated characteristic index, argument length}
const char sr5010MapArgsTab[][2] = {
  {0, 2}, {0, 7},
  {1, 2}, {1, 4},
  {2, 2}, {2, 3},
  {3, 2}, {3, 2}, {3, 5}, {3, 7},
  {4, 2}, {4, 2}, {4, 5}
};
*/

/*
//{incoming argument formats}
const char sr5010MapArgsIn[] = {
  's', //contains only a string
  's',
  's',
  's',
  's',
  'd' //contains a decimal value and possibly a string
}
*/

#define SR5010_NUMCHARS (sizeof(sr5010Map)/sizeof(sr5010_property_map))

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
  //Serial.setDebugOutput(true);
  //esp_log_level_set("*",ESP_LOG_VERBOSE);

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

  keyOld = 0;

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
//=====================================
  
  //Serial.println("Loop");

  if(millis() >= time_now + 100) //every 100ms
  {
    //Serial.println("KeyScan");
    
    keyNew = analogKeypadUpdate(keypadPin);
    if(keyOld != keyNew)
    {
      Serial.printf("Key: %u\n", keyNew);
      keyPressed = true;
      keyOld = keyNew;
    }

    time_now = millis();
  }

  if(keyPressed)
  {
    switch(keyNew)
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
        keySuppress = true;
      break;
    }

    if(!keySuppress)
    {
      if(keyNew <= 5)
      {
        Serial.printf("-> %.5s\n", strTemp);
        Serial2.printf("%.5s\r", strTemp);
      }
      
      if(keyNew >= 6)
      {
        Serial.printf("-> %.9s\n", strTemp);
        Serial2.printf("%.9s\r", strTemp);
      }

      keySuppress = false;
    }
    
    keyPressed = false;
  }
//=====================================

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
  
  if(updateBitfield)
  {
    UIUpdate(updateBitfield);
    updateBitfield = 0;
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
          //itoa(j, statusStr, 10);
          sprintf(statusStr, "%d", j);
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

int keypadLut[] = {
  1023, 1010, 850, 765,
  642, 605, 567, 538,
  474, 442, 429, 398,
  362, 287, 231, 199, 0
};

char analogKeypadUpdate(int analogPin)
{
  int analogValue = 0;
  char i;
  char key = 0;
  char retVal;
  int diff[16];
  
  for(i=0; i<8; i++)
  {
    analogValue += analogRead(keypadPin) >> 2; //reduce to 10-bit value
  }
  analogValue /= 8; //get oversampled average.

  //Serial.printf("anaVal:%d\n", analogValue);
  
  for(i=0; i<(sizeof(keypadLut)/sizeof(keypadLut[0])); i++)
  {
    if(analogValue <= keypadLut[i])
    {
      diff[i] = keypadLut[i] - analogValue;
    }
    else diff[i] = analogValue - keypadLut[i];
  }

  char minimumIndex = 0;
  for(i=0; i<(sizeof(keypadLut)/sizeof(keypadLut[0])); i++)
  {
    if(diff[i] < diff[minimumIndex]) minimumIndex = i;
  }
  //Serial.printf("minIdx=%u\n", minimumIndex);
  retVal = minimumIndex;
  
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

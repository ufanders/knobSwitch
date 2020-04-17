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

struct sr5010_property_map {
  char uuid[37];
  char statusStr[9]; //up to 8-character prefix (wtf!)
  int numArgs;
};

const sr5010_property_map sr5010Map[] = {
  {"bb0dae34-05cb-4290-a8c4-6dad813e82e2", "PW", 2},
  {"88e1ef43-9804-411d-a1f9-f6cdbda9a9a9", "MV", 2},
  {"60ef9a3d-5a67-464a-b7bf-fe1c187828f1", "MU", 2},
  {"c21d2540-fe05-43d7-b929-54b76140e030", "SI", 4},
  {"a6b89a0b-c740-4b82-bba5-df717b4163d5", "SV", 4},
  {"0969d2b6-60d8-4a1b-a88c-8457449a453e", "TFHD", 0},
  {"0969d2b6-60d8-4a1b-a88c-8457449a453e", "TFAN", 0},
  {"153acaae-9181-48c5-908c-21e81d3a8f85", "Z2", 2},
};

//{outgoing argument strings}
const char sr5010MapArgsOut[][8] = {
  "ON", "STANDBY",
  "UP", "DOWN",
  "ON", "OFF",
  "CD", "TV", "TUNER", "HDRADIO",
  "OFF", "TV", "BD", "V.AUX",
  "ON", "OFF"
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

char rxBuf[32], rxBufIdx, rxBufLen;
char rxBuf1[32], rxBufIdx1, rxBufLen1;

bool deviceConnected = false;
bool oldDeviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {

      //INFO: https://github.com/espressif/arduino-esp32/issues/3153
      
      std::string val = pCharacteristic->getValue();
      std::string strUUID = pCharacteristic->getUUID().toString();

      Serial.printf("~> %s: %s\n", strUUID.c_str(), val.c_str());

      //fetch command string index using UUID
      char i = 0;
      i = sr5010_searchByUUID(strUUID.c_str());
      //Serial.printf("%u\n",i);
      
      if(i != 0xFF)
      {
        int valNum = atoi(val.c_str()); // get argument index
  
        char j = 0;
        char argIndex = 0;
  
        //find argument string major index
        for(j = 0; j < i; j++)
        {
          argIndex += sr5010Map[j].numArgs;
        }
        argIndex += valNum; //add argument string minor index
      
        //write to serial port.
        Serial.printf("-> %s%s\n", sr5010Map[i].statusStr, sr5010MapArgsOut[argIndex]);
        Serial2.printf("%s%s\r", sr5010Map[i].statusStr, sr5010MapArgsOut[argIndex]);
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
  Serial.setDebugOutput(true);
  esp_log_level_set("*",ESP_LOG_VERBOSE);

  //disable M5Stack speaker clicking noise.
  dacWrite(25,0);

  // initialize the M5Stack object
  M5.begin();
  /*
    Power chip connected to gpio21, gpio22, I2C device
    Set battery charging voltage and current
    If used battery, please call this function in your project
  */
  M5.Power.begin();
  M5.Lcd.setBrightness(200);
  M5.Lcd.setTextWrap(true, true);

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
  Serial.println("Started LCD.");
  //M5.Lcd.println("Started LCD.");
  //M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
  M5.update();
  
  Serial.begin(115200);
  Serial.println("Starting BLE.");

  Serial2.begin(9600, SERIAL_8N1, 16, 17); //pin 16=TXD, pin 17=RXD.

  //INFO: https://github.com/nkolban/esp32-snippets/issues/144
  BLEDevice::init("SR5010");
  pServer = BLEDevice::createServer();
  //INFO: https://github.com/nkolban/esp32-snippets/issues/114
  //INFO: https://github.com/espressif/esp-idf/issues/1087
  pService = pServer->createService(BLEUUID(SERVICE_UUID), 10); //SR5010_NUMCHARS);
  BLECharacteristic *pCharacteristic;

  printDeviceAddress();
  
  Serial.println("Creating characteristics:");
  int i;
  for(i = 0; i<SR5010_NUMCHARS; i++)
  {
    Serial.println(sr5010Map[i].uuid);
    sr5010_chars_ptr[i] = pService->createCharacteristic(sr5010Map[i].uuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE \
    | BLECharacteristic::PROPERTY_NOTIFY); // | BLECharacteristic::PROPERTY_INDICATE);

    // Create a BLE Descriptor so notifications work.
    sr5010_chars_ptr[i]->addDescriptor(new BLE2902());
  
    sr5010_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }
  Serial.printf("Created %u characteristics.\n", i);

  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  Serial.println("Started BLE.");

  //read/refresh all statuses.
  for(i=0; i<SR5010_NUMCHARS; i++)
  {
    Serial.printf("-> %s?\n", sr5010Map[i].statusStr);
    Serial2.printf("%s?\r", sr5010Map[i].statusStr);
    while(!receiveUpdateSerial());
    processUpdateSerial(rxBuf);
  }

  keyOld = 0;

  time_now = millis();
  time_now10 = millis();
  sleepTimerExpired = false;
  updateLcd = true;
}

RTC_DATA_ATTR char UIStatePrevious;
RTC_DATA_ATTR char UIStateCurrent = 0;
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
    Serial.printf("UIStateCurrent=%d\n", UIStateCurrent); //DEBUG
    
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
  char i;

  while(ptr != NULL)
  {
    //parse update string and update corresponding characteristic value.
    char statusStr[16];

    i = sr5010_searchByCmd(ptr);

    if(i != 0xff)
    {
      lenCmd = strlen(sr5010Map[i].statusStr);
      lenArg = strlen(ptr) - lenCmd;
      
      memcpy(statusStr, &ptr[lenCmd], lenArg); //separate base command from argument.
      statusStr[lenArg] = '\0'; //terminate string.
      Serial.printf("[i] cmd: %s, len: %d, arg: %s, len: %d\n", ptr, lenCmd, statusStr, lenArg);

      //TODO: check if argument is numeric. Store as string in characteristic value.
      if(isDigit(statusStr[0]))
      {
        sr5010_chars_ptr[i]->setValue(statusStr);
        match = 1;
      }
      else
      {
        //TODO: if not, compare argument against arg string list.
        char j = sr5010_searchByArg(i, statusStr);
        if(j != 0xFF)
        {
          //TODO: if a match is found, insert the arg string index as characteristic value.
          itoa(j, statusStr, 10);
          sr5010_chars_ptr[i]->setValue(statusStr);
          match = 1;
        }
      }

      if(match)
      {
        Serial.printf("^ %s\n", sr5010_chars_ptr[i]->getValue().c_str());
        sr5010_chars_ptr[i]->notify();
      }
      else Serial.println("\nNo Arg match.");

      if(i < 5)
      {
        //trigger UI update.
        UIStateCurrent++; //Just something to keep registering a difference.
        updateBitfield |= (1 << i);
        Serial.printf("updateBitfield = 0x%X\n", updateBitfield);
      }
    }
    else Serial.println("\nNo Cmd match.");

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
    len = strlen(sr5010Map[i].statusStr);
    c = memcmp(ptrCmd, sr5010Map[i].statusStr, len);
    
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

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  int j = 0; 
  int c = 0;
  int match = 0;
  char len = 0;

  //take length of each available command string.
  //compare over length of that string.

  Serial.printf("ptrArg: %s\nArgs: ", ptrArg);

  for(i=0; i<cmdIndex; i++)
  {
    //skip to args for incoming command.
    j += sr5010Map[i].numArgs;
  }

  i = 0;  
  do
  {
    len = strlen(sr5010MapArgsOut[j]);
    Serial.printf("%s | ", sr5010MapArgsOut[j]);
    
    c = memcmp(ptrArg, sr5010MapArgsOut[j], len);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else j++;

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
  
  for(field = 0; field < 5; field++)
  {
    if(bitField & (1 << field))
    {
      refreshNeeded = true;

      argIndex = 0;
      if(field != 1) //Skip volume field, volume is stored as an integer.
      {
        //find argument string major index
        for(j = 0; j < field; j++)
        {
          argIndex += sr5010Map[j].numArgs;
        }
        //argIndex += sr5010MapLocalState[field]; //add argument string minor index
        argIndex += atoi(sr5010_chars_ptr[field]->getValue().c_str());
      }

      M5.Lcd.fillRect(0, (10*field), M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, (10*field));
      
      switch(field)
      {
        case 0: //Power
          M5.Lcd.printf("Power: %s", sr5010MapArgsOut[argIndex]);
        break;

        case 1: //Volume (stored as integer, not index)
          //M5.Lcd.printf("Volume: %d", sr5010MapLocalState[field]);
          M5.Lcd.printf("Volume: %s",sr5010_chars_ptr[field]->getValue().c_str());
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

        case 7: //Zone 2
          M5.Lcd.printf("Zone2: %s", sr5010MapArgsOut[argIndex]);
        break;

        default:
        break;
      }
    }
  }

  if(refreshNeeded) 
  {
    //M5.update();
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

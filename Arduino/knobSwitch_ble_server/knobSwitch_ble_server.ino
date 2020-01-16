#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string.h>
#include <stdio.h>

int keypadPin = 36;
char keyOld;
unsigned long time_now = 0;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "18550d7d-d1aa-4968-a563-e8ebeb4840ea"

struct sr8500_property_map {
  char uuid[37];
  char statusStr[4];
};

//NOTE: The STANDBY MODE must be set to NORMAL 
//instead of ECONOMY for the power function to work correctly.

//we want to keep this info in flash, not in RAM.
const sr8500_property_map sr8500Map[] = {
  {"7b0819db-21f6-429d-ad5a-c31215fc7114", "PWR"},
  {"1ffacfe7-6051-42b1-8dab-004e164a82cd", "VOL"},
  {"d1cf0f01-a78d-4de0-9812-bd391bcba635", "AMT"},
  {"18a36e61-32fb-4f9d-932f-75c246f1f881", "TOB"},
  {"01f55e00-5b3b-443f-8ca2-24f2b3ca72fb", "TOT"},
  {"d33efca9-e30a-4e41-98fc-d5c84e2b3f77", "SRC"},
  {"d878b661-6cae-48e8-a84c-3952f89f5623", "71C"}, 
  {"25f76a7b-87e3-498e-9abf-290fb89c16c7", "INP"},
  {"c7d89e80-42b2-4b0e-8b6b-a783258e48e5", "ATT"}, 
  {"b7dae477-dd0d-4f0e-afdb-229e3bdb983a", "SUR"}, 
  {"6ac062d9-b0a2-4483-800e-a6497c7365ab", "THX"}, 
  {"f4544ed7-17fa-4d7d-a826-8513132eb31b", "SPK"}, 
  {"15095cae-1aca-423b-ba62-609f8bf6c0ac", "TFQ"}, 
  {"a89367a0-9e05-461d-9a07-10b8135f3a46", "TPR"}, 
  {"7e8f0681-a312-4934-9300-65de924e4ff0", "TMD"}, 
  {"ef8ce36d-1e04-4d4a-845d-d71707201257", "MNU"},
  {"8981aff4-52c3-4e3b-9be6-dc11af1d4971", "CUR"},
  {"5e30b0bc-d3a3-45a6-a95b-ad5bfb6647a5", "TTO"}, 
  {"1bf5ef56-90d4-46d5-87b8-35c6d071a01b", "CHL"}, 
  {"eeea6f37-db29-44d0-bd4d-7c45641fa92f", "NGT"}, 
  {"f46b3732-cd2c-4a3d-8301-1b7167d7fd54", "REQ"}, 
  {"6535a6f2-961a-4f7c-af67-6192be58bd8e", "SLP"}, 
  {"b276067f-6e05-44ef-8231-2853a8f6a405", "SIG"}, 
  {"763cf4bc-e298-4fe1-b70a-f2029ff21f8d", "SFQ"}, 
  {"fb7478cf-7f59-4056-9796-ae9768956b4e", "CHS"},
  {"7fb93eb2-ed8f-4051-9497-15feb0e0db84", "MPW"}, 
  {"58110e30-5916-4c04-a71e-729ccd58147b", "MVL"}, 
  {"ee4b9c81-d628-415d-9fd2-b53e3bb2280b", "MAM"}, 
  {"93521329-e117-41c7-97c7-e3cd466457e7", "MVS"}, 
  {"1ecd0948-e5f9-402c-b342-611417ccf8cc", "MSP"}, 
  {"dd3361c4-a86a-42f3-8d77-8caaad5ded17", "MSV"}, 
  {"f1b68440-406c-4c0b-8b52-921947143d2b", "MSM"}, 
  {"d72a2014-1cc1-4fbc-ba51-ec07dc4c4af4", "MST"},  
  {"c67b1a61-1ac9-43b0-a5c1-8228ab12effd", "MSC"}, 
  {"34eba641-6f2e-446a-8755-ea1d0fd30d62", "MTF"}, 
  {"7bb61d7e-5cac-4b90-8982-1972975b12b9", "MTP"}, 
  {"5731a6c9-64c6-41ee-9fd3-0cc3d0bd7692", "MTM"},
  {"85cd9a55-5766-4a26-aac4-be7054b0c4b0", "OSD"},
  {"44e017cb-f131-4c56-a75e-3f1b499319ac", "AST"}
};

#define SR8500_NUMCHARS (sizeof(sr8500Map)/sizeof(sr8500_property_map))

BLECharacteristic* sr8500_chars_ptr[SR8500_NUMCHARS];

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
  {"a6b89a0b-c740-4b82-bba5-df717b4163d5", "SV", 3},
  {"0969d2b6-60d8-4a1b-a88c-8457449a453e", "TFHD", 0}
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

//{outgoing argument strings}
const char sr5010MapArgsOut[][8] = {
  "ON", "STANDBY",
  "UP", "DOWN",
  "ON", "OFF",
  "CD", "TV", "TUNER", "HDRADIO",
  "TV", "BD", "V.AUX"
};

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

char sr8500_searchByUUID(const char*);
char sr8500_searchByCmd(char*);

char rxBuf[32], rxBufIdx, rxBufLen;

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
      Serial.printf("i=%u\n", i);
      
      if(i != 0xFF)
      {
        int valNum = atoi(val.c_str()); // get argument index
        Serial.println(valNum);
  
        char j = 0;
        char argIndex = 0;
  
        //find argument string major index
        for(j = 0; j < i; j++)
        {
          argIndex += sr5010Map[j].numArgs;
        }
        argIndex += valNum; //add argument string minor index
        Serial.println(argIndex);
      
        //write to serial port.
        Serial.printf("-> %s%s\n", sr5010Map[i].statusStr, sr5010MapArgsOut[argIndex]);
        Serial1.printf("%s%s\r", sr5010Map[i].statusStr, sr5010MapArgsOut[argIndex]);
      }
    };
};

BLEServer *pServer;
BLEService *pService;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE.");

  Serial1.begin(9600, SERIAL_8N1, 5, 4); //pin 4=TXD, pin 5=RXD.

  BLEDevice::init("Marantz SR5010");
  pServer = BLEDevice::createServer();
  pService = pServer->createService(BLEUUID(SERVICE_UUID), SR5010_NUMCHARS, 0);
  BLECharacteristic *pCharacteristic;
  
  int i;
  for(i = 0; i<SR5010_NUMCHARS; i++)
  {
    sr5010_chars_ptr[i] = pService->createCharacteristic(sr5010Map[i].uuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    sr5010_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }

/*
  for(int i = SR5010_NUMCHARS; i<SR5010_NUMCHARS; i++)
  {
    sr5010_chars_ptr[i] = pService->createCharacteristic(sr5010Map[i].uuid,
    BLECharacteristic::PROPERTY_READ);
    sr5010_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }
*/
  
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Started BLE.");
  
  //read/refresh all statuses.
  for(i=0; i<SR5010_NUMCHARS-2; i++)
  {
    Serial.printf("-> %s?\n", sr5010Map[i].statusStr);
    Serial1.printf("%s?\r", sr5010Map[i].statusStr);
    while(!receiveUpdateSerial());
    processUpdateSerial(rxBuf);
  }

  keyOld = 0;

  time_now = millis();
}

void loop() {
//=====================================
  char keyNew = 0;
  bool keyPressed = false;
  char strTemp[16] = "";

  if(millis() >= time_now + 100) //every 100ms
  {
    keyNew = analogKeypadUpdate(keypadPin);
    if(keyOld != keyNew)
    {
      Serial.printf("%u\n", keyNew);
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
      
      case 8:

      break;

      default:
      break;
    }

    if(keyNew <= 5)
    {
      Serial.printf("-> %.5s\n", strTemp);
      Serial1.printf("%.5s\r", strTemp);
    }
    
    if(keyNew >= 6)
    {
      Serial.printf("-> %.9s\n", strTemp);
      Serial1.printf("%.9s\r", strTemp);
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
  if(receiveUpdateSerial()) processUpdateSerial(rxBuf);
}

int processUpdateSerial(char* strUpdate)
{
  Serial.printf("<- %s\n", strUpdate); //print incoming string to console.
  
  //isolate multiple updates within one response using '@' token.
  int init_size = strlen(strUpdate);
  char delim[] = "\r";
  char len = 0;

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
      match = 1;
      len = strlen(ptr);
      memcpy(statusStr, &ptr[2], len-2); //isolate last character(s).
      statusStr[len-2] = '\0';
      Serial.printf("Matched %.2s, len: %d value: %s\n", ptr, len, statusStr);
      
      //update characteristic value with received status value.
      sr5010_chars_ptr[i]->setValue(statusStr);
    }
    else Serial.println("\nNo match.");

    ptr = strtok(NULL, delim); //parse next token.
  }

  return 0;
}

int receiveUpdateSerial(void)
{
  char rc;

  //receive any new status update.
  if(Serial1.available())
  {
    rxBufIdx = 0; rxBufLen = 0;
    
    //read in characters until we see a CR.
    while(Serial1.available())
    {
      rc = Serial1.read();
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

char sr8500_searchByCmd(char* ptrCmd)
{
  char retVal = 0xFF; //No match found.

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  int c = 0;
  int match = 0;
  while(i<SR8500_NUMCHARS && !match)
  {
    c = memcmp(ptrCmd, sr8500Map[i].statusStr, 3);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else i++;
  }

  return retVal;
}

char sr8500_searchByUUID(const char* ptrUUID)
{
  char retVal = 0xFF; //No match found.

  //search for the command the written characteristic maps to.
  int i = 0;
  int c = 0;
  int match = 0;
  while(i<SR8500_NUMCHARS && !match)
  {
    c = strcmp(ptrUUID, sr8500Map[i].uuid);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else i++;
  }

  return retVal;
}

char sr5010_searchByCmd(char* ptrCmd)
{
  char retVal = 0xFF; //No match found.

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  int c = 0;
  int match = 0;
  while(i<SR5010_NUMCHARS && !match)
  {
    c = memcmp(ptrCmd, sr5010Map[i].statusStr, 2);
    
    if(c == 0) //we found a match.
    {
      match = 1;
      retVal = i;
    }
    else i++;
  }

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
    Serial.printf("ptrUUID=%s\n", ptrUUID);
    Serial.printf("sr5010Map.uuid==%s\n", sr5010Map[i].uuid);
    
    c = strcmp(ptrUUID, sr5010Map[i].uuid);
    Serial.printf("strcmp=%u\n", c);
    
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
  1023, 970, 850, 765,
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
  
  for(i=0; i<16+1; i++)
  {
    if(analogValue <= keypadLut[i])
    {
      diff[i] = keypadLut[i] - analogValue;
    }
    else diff[i] = analogValue - keypadLut[i];
  }

  char minimumIndex = 0;
  for(i=0; i<16+1; i++)
  {
    if(diff[i] < diff[minimumIndex]) minimumIndex = i;
  }
  //Serial.printf("minIdx=%u\n", minimumIndex);
  retVal = minimumIndex;
  
  return retVal;
}

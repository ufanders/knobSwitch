#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string.h>
#include <stdio.h>
#include <BLE2902.h>

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

char sr8500_searchByUUID(const char*);
char sr8500_searchByCmd(char*);

BLECharacteristic* sr8500_chars_ptr[SR8500_NUMCHARS];
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
      
      std::string val = pCharacteristic->getValue();
      std::string strUUID = pCharacteristic->getUUID().toString();

      //fetch command string using UUID
      char i = 0;
      i = sr8500_searchByUUID(strUUID.c_str());

      if(i != 0xFF)
      {
        //write to serial port.
        Serial.printf("-> @%.3s:%s\n", sr8500Map[i].statusStr, val.c_str());
        Serial1.printf("@%.3s:%s\r", sr8500Map[i].statusStr, val.c_str());
      }
    };
};

BLEServer *pServer;
BLEService *pService;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE.");

  Serial1.begin(9600, SERIAL_8N1, 5, 4); //pin 4=TXD, pin 5=RXD.

  BLEDevice::init("Marantz SR8500");
  pServer = BLEDevice::createServer();
  pService = pServer->createService(BLEUUID(SERVICE_UUID), SR8500_NUMCHARS, 0);
  BLECharacteristic *pCharacteristic;
  
  int i;
  for(i = 0; i<SR8500_NUMCHARS-4; i++)
  {
    sr8500_chars_ptr[i] = pService->createCharacteristic(sr8500Map[i].uuid, \
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE \
    | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);
    sr8500_chars_ptr[i]->addDescriptor(new BLE2902());
    sr8500_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }

  for(int i = SR8500_NUMCHARS-4; i<SR8500_NUMCHARS; i++)
  {
    sr8500_chars_ptr[i] = pService->createCharacteristic(sr8500Map[i].uuid, \
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY \
    | BLECharacteristic::PROPERTY_INDICATE);
    sr8500_chars_ptr[i]->addDescriptor(new BLE2902());
    sr8500_chars_ptr[i]->setCallbacks(new MyCallbacks());
  }
  
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Started BLE.");
  
  Serial1.print("@AST:F\r"); //enable auto status update for all layers.
  
  //read/refresh all statuses.
  for(i=0; i<SR8500_NUMCHARS-1; i++) //skip AST as we've already received its status.
  {
    Serial.printf("-> @%.3s:?\n", sr8500Map[i].statusStr);
    Serial1.printf("@%.3s:?\r", sr8500Map[i].statusStr);
    while(!receiveUpdateSerial());
    processUpdateSerial(rxBuf);
  }
}

void loop() {

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
  char delim[] = "@";
  char len = 0;

  char *ptr = strtok(strUpdate, delim);
  int match;
  char i;

  while(ptr != NULL)
  {
    //parse update string and update corresponding characteristic value.
    char statusStr[16];

    i = sr8500_searchByCmd(ptr);

    if(i != 0xff)
    {
      match = 1;
      len = strlen(ptr);
      memcpy(statusStr, &ptr[4], len-4); //isolate last character(s).
      statusStr[len-4] = '\0';
      Serial.printf("Matched %.3s, len: %d value: %s\n", ptr, len, statusStr);
      
      //update characteristic value with received status value.
      sr8500_chars_ptr[i]->setValue(statusStr);
      //sr8500_chars_ptr[i]->notify();
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

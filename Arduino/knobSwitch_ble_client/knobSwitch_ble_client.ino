#include "BLEDevice.h"
#include "BLEScan.h"
#include <SimpleRotary.h>
#include <M5Stack.h>

unsigned long time_now = 0;

// Pin A, Pin B, Button Pin
SimpleRotary rotary(35,36,26);

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
  /*{"18a36e61-32fb-4f9d-932f-75c246f1f881", "TOB"},
  {"01f55e00-5b3b-443f-8ca2-24f2b3ca72fb", "TOT"},*/
  {"d33efca9-e30a-4e41-98fc-d5c84e2b3f77", "SRC"},
  /*{"d878b661-6cae-48e8-a84c-3952f89f5623", "71C"}, 
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
  {"44e017cb-f131-4c56-a75e-3f1b499319ac", "AST"}*/
};

#define SR8500_NUMCHARS (sizeof(sr8500Map)/sizeof(sr8500_property_map))

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

#define SR5010_NUMCHARS (sizeof(sr5010Map)/sizeof(sr5010_property_map))

char sr5010_searchByUUID(const char*);
char sr5010_searchByCmd(char*);

//keeps track of all local control states.
char sr5010MapLocalState[SR5010_NUMCHARS];

BLECharacteristic* sr5010_chars_ptr[SR5010_NUMCHARS];

char sr8500_searchByUUID(const char*);
char sr8500_searchByCmd(char*);

// The remote service we wish to connect to.
static BLEUUID serviceUUID(SERVICE_UUID);

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;
static BLERemoteService* pRemoteService;

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify: ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" len: ");
    Serial.println(length);
    Serial.print(" val: ");
    Serial.println((char*)pData);
    M5.Lcd.printf("Notify: %s, len: %d, val: %c\n", \
    pBLERemoteCharacteristic->getUUID().toString().c_str(), length, pData);
    M5.update();
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("Connected.");
    M5.Lcd.println("Connected.");
    M5.Lcd.printf("%lums\n", time_now);
    M5.update();
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected.");
    M5.Lcd.println("Disconnected.");
    M5.update();
  }
};

const char serverAddress[] = "b4:e6:2d:d9:fb:47";

bool connectToServer() {

    BLEClient*  pClient  = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server.
    Serial.printf("Connecting to %s", serverAddress);
    //Serial.println(myDevice->getAddress().toString().c_str());
    pClient->connect(BLEAddress(serverAddress)); //myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)

    // Obtain a reference to the service we are after in the remote BLE server.
    pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == NULL) {
      Serial.println("Service not found.");
      M5.Lcd.println("Service not found.");
      M5.update();
      pClient->disconnect();
      return false;
    }
    Serial.println("Service found.");
    M5.Lcd.println("Service found.");
    M5.update();

/*
    //register all characteristics for realtime notification.
    for(int i = 0; i < SR5010_NUMCHARS; i++)
    {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[i].uuid);
        if (pRemoteCharacteristic != nullptr) 
        {
          if(pRemoteCharacteristic->canNotify())
          {
            Serial.printf("%s can notify.\n", sr5010Map[i].uuid);
            M5.Lcd.printf("%s can notify.\n", sr5010Map[i].uuid);
            M5.update();
            pRemoteCharacteristic->registerForNotify(notifyCallback, true);
          }
          else
          {
            Serial.printf("%s can NOT notify.\n", sr5010Map[i].uuid);
            M5.Lcd.printf("%s can NOT notify.\n", sr5010Map[i].uuid);
            M5.update();
          }
        }
    }
    */
 
    connected = true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

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
    c = memcmp(ptrCmd, sr5010Map[i].statusStr, 3);
    
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

void setup() {

  int i = 0;
  time_now = millis();

  //pins are pulled up by hardware and inputs at POR.
  //pinMode(39, INPUT_PULLUP); //Button A
  //pinMode(38, INPUT_PULLUP); //Button B
  //pinMode(37, INPUT_PULLUP); //Button C

  rotary.setErrorDelay(100);

  for(i = 0; i<SR5010_NUMCHARS; i++)
  {
    sr5010MapLocalState[i] = 0;
  }

  // initialize the M5Stack object
  M5.begin();

  // text print
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  Serial.println("Started LCD.");
  M5.Lcd.println("Started LCD.");
  M5.update();
  
  Serial.begin(115200);

  Serial.println("Starting BLE.");
  M5.Lcd.println("Starting BLE.");
  M5.update();
  BLEDevice::init("");

  /*
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  */

  doConnect = true;
  
  //esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,1); //1 = High, 0 = Low

  //If you were to use ext1, you would use it like
  //esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);

  //Go to sleep now
  //Serial.println("Going to sleep now");
  //esp_deep_sleep_start();
} // End of setup.


// This is the Arduino main loop function.
void loop() {

  if(connected)
  {    
    //TODO: poke remote characteristics per the control being operated.
    char i;
    char c;
    char txBuf[16];

    BLERemoteCharacteristic* pRemoteCharacteristic;

    i = digitalRead(39);
    if(!i) //button A
    {
      sr5010MapLocalState[0] ^= 1; //toggle power state.
      c = '0' + sr5010MapLocalState[0];
      
      //Get remote characteristic.
      if(pRemoteService != nullptr)
      {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[0].uuid);
        if (pRemoteCharacteristic == nullptr) {
          Serial.println("Can't find characteristic.");
        }
        else
        {
          Serial.printf("-> %s: ", sr5010Map[0].uuid);
      
          //Write value to remote characteristic.
          if(pRemoteCharacteristic->canWrite()) {
            sprintf(txBuf, "%c\r", c);
            Serial.println(txBuf);
            pRemoteCharacteristic->writeValue(txBuf);
          }
  
          //read remote 
        }
      }

      while(!digitalRead(39)); 
    }
    
    
    i = rotary.rotate(); // 0 = not turning, 1 = CW, 2 = CCW
    if(i) //rotary encoder rotation
    {
      if (i == 1)
      {
        //Serial.println("CW");
        c = '0'; //volume up.
      }
      else
      {
        //Serial.println("CCW");
        c = '1'; //volume down.
      }

      //Get remote characteristic.
      if(pRemoteService != nullptr)
      {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[1].uuid);
        if (pRemoteCharacteristic == nullptr) {
          Serial.println("Can't find characteristic.");
        }
        else
        {
          Serial.printf("-> %s: ", sr5010Map[1].uuid);
      
          //Write value to remote characteristic.
          if(pRemoteCharacteristic->canWrite()) {
            sprintf(txBuf, "%c\r", c);
            Serial.println(txBuf);
            pRemoteCharacteristic->writeValue(txBuf);
          }
        }
      }
    }

    if(rotary.push()) //rotary encoder button.
    {
      sr5010MapLocalState[2] ^= 1; //toggle mute state.
      c = '0' + sr5010MapLocalState[2];
      
      //Get remote characteristic.
      if(pRemoteService != nullptr)
      {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[2].uuid);
        if (pRemoteCharacteristic == nullptr) {
          Serial.println("Can't find characteristic.");
        }
        else
        {
          Serial.printf("-> %s: ", sr5010Map[2].uuid);
      
          //Write value to remote characteristic.
          if(pRemoteCharacteristic->canWrite()) {
            sprintf(txBuf, "%c\r", c);
            Serial.println(txBuf);
            pRemoteCharacteristic->writeValue(txBuf);
          }
        }
      }
    }

    i = digitalRead(38);
    if(!i) //button B
    {
      sr5010MapLocalState[3]++; //increment input state.
      if(sr5010MapLocalState[3] >= sr5010Map[3].numArgs) sr5010MapLocalState[3] = 0;
      
      c = '0' + sr5010MapLocalState[3];
      
      //Get remote characteristic.
      if(pRemoteService != nullptr)
      {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[3].uuid);
        if (pRemoteCharacteristic == nullptr) {
          Serial.println("Can't find characteristic.");
        }
        else
        {
          Serial.printf("-> %s: ", sr5010Map[0].uuid);
      
          //Write value to remote characteristic.
          if(pRemoteCharacteristic->canWrite()) {
            sprintf(txBuf, "%c\r", c);
            Serial.println(txBuf);
            pRemoteCharacteristic->writeValue(txBuf);
          }
  
          //read remote 
        }
      }

      while(!digitalRead(38)); 
    }

    if(0) //selector switch
    {
      
    }
    
  }

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("BLE Connected.");
    } else {
      Serial.println("BLE not connected.");
    }
    doConnect = false;
  }
  
  if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  M5.update();

} // End of loop

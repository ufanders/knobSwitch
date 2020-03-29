#include "BLEDevice.h"
#include "BLEScan.h"
#include <SimpleRotary.h>
#include <M5Stack.h>
#include <string.h>

unsigned long time_now = 0;
char batteryLevel, retries;
volatile char updateBitfield;
bool sleepTimerExpired, updateLcd;

#define bitPwr (1 << 0)
#define bitVolume (1 << 1)
#define bitMute (1 << 2)
#define bitSrcAudio (1 << 3)
#define bitSrcVideo (1 << 4)

// Pin A, Pin B, Button Pin
SimpleRotary rotary(35,36,26);

#define SERVICE_UUID "18550d7d-d1aa-4968-a563-e8ebeb4840ea"

//NOTE: The STANDBY MODE must be set to NORMAL 
//instead of ECONOMY for the power function to work correctly.

//we want to keep this info in flash, not in RAM.
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

#define SR5010_NUMCHARS (sizeof(sr5010Map)/sizeof(sr5010_property_map))

char sr5010_searchByUUID(const char*);
char sr5010_searchByCmd(char*);

//keeps track of all local control states.
int sr5010MapLocalState[SR5010_NUMCHARS];

BLECharacteristic* sr5010_chars_ptr[SR5010_NUMCHARS];

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

  Serial.println("Notify.");
  Serial.printf("pData: %s\n", (const char*)pData);
  
  std::string strUUID = pBLERemoteCharacteristic->getUUID().toString();

  char charIndex;
  charIndex = sr5010_searchByUUID(strUUID.c_str());
  sr5010MapLocalState[charIndex] = atoi((const char*)pData);
  
  Serial.printf("Char[%d]: %d\n", charIndex, sr5010MapLocalState[charIndex]);
  updateBitfield |= (1 << charIndex);
  updateLcd = true;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("Connected.");
    Serial.printf("%lums\n", time_now);

    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(0, M5.Lcd.height()-11, M5.Lcd.width()-1, 10, GREEN);
    M5.Lcd.setCursor(0, M5.Lcd.height()-11);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.print("Connected");
    M5.Lcd.setTextColor(WHITE);
    updateLcd = true;
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected.");
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(0, M5.Lcd.height()-11, M5.Lcd.width()-1, 10, RED);
    M5.Lcd.setCursor(0, M5.Lcd.height()-11);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.print("Disconnected");
    M5.Lcd.setTextColor(WHITE);
    updateLcd = true;
  }
};

const char serverAddress[] = "24:0A:C4:A4:52:22"; //"b4:e6:2d:d9:fb:47";

bool connectToServer() {

    BLEClient*  pClient  = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server.
    Serial.printf("Connecting to %s.\n", serverAddress);
    //Serial.println(myDevice->getAddress().toString().c_str());
    pClient->connect(BLEAddress(serverAddress)); //myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)

    // Obtain a reference to the service we are after in the remote BLE server.
    pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == NULL) {
      Serial.println("Service not found.");
      pClient->disconnect();
      updateLcd = true;
      return false;
    }
    Serial.println("Service found.");

    int i = 0;

    //register all characteristics for realtime notification.
    for(i = 0; i < SR5010_NUMCHARS; i++)
    {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[i].uuid);
        if (pRemoteCharacteristic != nullptr) 
        {
          if(pRemoteCharacteristic->canNotify())
          {
            Serial.printf("%s can notify.\n", sr5010Map[i].uuid);
            pRemoteCharacteristic->registerForNotify(notifyCallback, true);
          }
          else
          {
            Serial.printf("%s can NOT notify.\n", sr5010Map[i].uuid);
          }
        }
    }

    //read remote
    for(i = 0; i < SR5010_NUMCHARS; i++)
    {
      pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[i].uuid);
      
      if (pRemoteCharacteristic != nullptr) 
      {
        if(pRemoteCharacteristic->canRead()) {
          std::string val = pRemoteCharacteristic->readValue();
          sr5010MapLocalState[i] = atoi(val.c_str());
          Serial.printf("<- %s\n ", val.c_str());
        }
      }
    }
 
    connected = true;
    updateLcd = true;
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
      if(field < 2) //Skip volume and power fields, volume is at index 0 and volume is stored as an integer.
      {
        //find argument string major index
        for(j = 0; j < field; j++)
        {
          argIndex += sr5010Map[j].numArgs;
        }
        argIndex += sr5010MapLocalState[field]; //add argument string minor index
      }

      M5.Lcd.fillRect(0, (10*field), M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, (10*field));
      
      switch(field)
      {
        case 0: //Power
          M5.Lcd.printf("Power: %s", sr5010MapArgsOut[argIndex]);
        break;

        case 1: //Volume (stored as integer, not index)
          M5.Lcd.printf("Volume: %d", sr5010MapLocalState[field]);
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

void setup() {

  int i = 0;
  time_now = millis();
  sleepTimerExpired = false;
  updateLcd = true;

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
  M5.Lcd.println("Started LCD.");
  M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
  M5.update();
  
  Serial.begin(115200);

  doConnect = true;
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
  
  //esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,1); //1 = High, 0 = Low

  //If you were to use ext1, you would use it like
  //esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);

  //Go to sleep now
  //Serial.println("Going to sleep now");
  //esp_deep_sleep_start();
} // End of setup.

RTC_DATA_ATTR char UIStatePrevious;

// This is the Arduino main loop function.
void loop() {

  //get all button input states at once.
  char i;
  i = rotary.rotate() | rotary.push() << 2 | (digitalRead(39) << 3) | \
  (digitalRead(38) << 4) | (digitalRead(37) << 5);

  if( i != UIStatePrevious)
  {
    if(sleepTimerExpired)
    {
      //wake up LCD, reconnect to BLE server and process UI input.
      M5.Lcd.wakeup();
      M5.Lcd.setBrightness(200);
      M5.Lcd.fillRect(0, M5.Lcd.height()-21, M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, M5.Lcd.height()-21);
      M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
      updateLcd = true;
      
      sleepTimerExpired = false;
    }
    
    time_now = millis();
  }
  UIStatePrevious = i;

  if(connected)
  {    
    //TODO: poke remote characteristics per the control being operated.
    char c;
    char txBuf[16];
    std::string val;

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
      time_now = millis();
      sleepTimerExpired = false;
      
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
        }
      }

      while(!digitalRead(38)); 
    }

    if(0) //selector switch
    {
      time_now = millis();
      sleepTimerExpired = false;
    }
  }

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("BLE Connected.");
      retries = 0;
      doConnect = false;
    }
    else 
    {
      Serial.println("Retrying...");
      retries++;
    }

    if(doConnect && (retries > 2))
    {
      Serial.println("BLE not connected.");
      doConnect = false;
    }
  }
  
  if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  if(!sleepTimerExpired && (millis() - time_now >= 10000))
  {
    sleepTimerExpired = true;
    Serial.println("Sleeping now.");
    M5.Lcd.setBrightness(0);
    M5.Lcd.sleep();
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

} // End of loop

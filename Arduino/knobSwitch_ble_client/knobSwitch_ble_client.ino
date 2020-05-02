#include "BLEDevice.h"
#include "BLEScan.h"
#include <string.h>
#include <M5Stack.h>
#include <AnalogKeypad.h>
#include <Wire.h> //I2C library

// SX1509 I2C I/O expander
#include <SparkFunSX1509.h>
const byte SX1509_ADDRESS = 0x3E;
SX1509 io;

//Rotary encoder
#include <SimpleRotary.h>
SimpleRotary rotary(2,5,36); // Pin A, Pin B, Button Pin

#include "knobSwitch.h"

#define bitPwr (1 << 0)
#define bitVolume (1 << 1)
#define bitMute (1 << 2)
#define bitSrcAudio (1 << 3)
#define bitSrcVideo (1 << 4)

unsigned long time_now = 0;
unsigned long time_now2 = 0;
char batteryLevel, retries;
volatile char updateBitfield;
bool sleepTimerExpired, updateLcd;
volatile bool updateAll;

struct avPreset
{
  //-1 indicates no change to the source.
  signed char audioSrc;
  signed char videoSrc;
};

int sendPreset(byte, byte);
int sr5010MapLocalStateGetText(char, bool, char*);
int sr5010PokeState(char, int);

const avPreset avPresets[16] = {
  //Video source buttons
  {-1, 2}, /*TV only*/ {-1, 4}, /*MPLAY only*/ {-1, 5}, /*GAME only*/ {-1, 6}, /*AUX1 only*/ 
  {-1, 1}, /*BD only*/ {-1, 0}, /*DVD only*/ {-1, 3}, /*SAT/CBL only*/ {-1, 2}, /*TV only*/ 
  //Audio source buttons
  {7, -1}, /*Tuner only*/ {0, -1}, /*CD only*/ {3, -1}, /*TV only*/ {5, -1}, /*MPLAY only*/ 
  {6, -1}, /*GAME only*/ {2, -1}, /*BD only*/ {8, -1}, /*AUX1 only*/ {1, -1} /*DVD only*/ 
};

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

  switch(charIndex)
  {
    case 0:
      updateBitfield |= bitPwr;
    break;

    case 2:
      updateBitfield |= bitVolume;
    break;

    case 3:
      updateBitfield |= bitMute;
    break;

    case 4:
      updateBitfield |= bitSrcAudio;
    break;

    case 5:
      updateBitfield |= bitSrcVideo;
    break;
    
    default:
    break;
  }

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
      if(!(sr5010Map[i].attributes & BIT_ISUPDOWN))
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
      else Serial.printf("%s will NOT notify.\n", sr5010Map[i].uuid);
    }

    //read remote
    for(i = 0; i < SR5010_NUMCHARS; i++)
    {
      Serial.printf("<~ %s: ", sr5010Map[i].uuid);
      
      pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[i].uuid);
      
      if(pRemoteCharacteristic != nullptr) 
      {
        if(!(sr5010Map[i].attributes & BIT_ISUPDOWN))
        {
          if(pRemoteCharacteristic->canRead()) {
              std::string val = pRemoteCharacteristic->readValue();
              sr5010MapLocalState[i] = atoi(val.c_str());
              Serial.printf("%d\n ", sr5010MapLocalState[i]);
            }
        }
        else Serial.println("is UP/DOWN, skipping.");
        
      }
      else Serial.println("not found.");
      
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
  char field, j, i, strOut[16];
  #define NUM_FIELDS 5
  char cmdIndices[NUM_FIELDS] = {0, 2, 3, 4, 5};
  
  for(field = 0; field < NUM_FIELDS; field++)
  {
    if(bitField & (1 << field))
    {
      M5.Lcd.fillRect(0, (10*field), M5.Lcd.width()-1, 10, BLACK);
      M5.Lcd.setCursor(0, (10*field));

      switch(field)
      {
        case 0: //Power
          sr5010MapLocalStateGetText(cmdIndices[field], false, strOut);
          M5.Lcd.printf("Power: %s", strOut);
        break;

        case 1: //Volume (stored as integer, not arg string index)
          sr5010MapLocalStateGetText(cmdIndices[field], true, strOut);
          M5.Lcd.printf("Volume: %s", strOut);
        break;

        case 2: //Mute
          sr5010MapLocalStateGetText(cmdIndices[field], false, strOut);
          M5.Lcd.printf("Mute: %s", strOut);
        break;

        case 3: //Audio source
          sr5010MapLocalStateGetText(cmdIndices[field], false, strOut);
          M5.Lcd.printf("Audio: %s", strOut);
          
          for(i=4; i<8; i++)
          {
            //clear LED indicators.
            io.analogWrite(i, 0);
            io.analogWrite(i+8, 0);
          }
          
          switch(sr5010MapLocalState[cmdIndices[field]]) //set indicator.
          {
            case 0: //CD
              io.analogWrite(4, 255);
            break;
            
            case 2: //BD
              io.analogWrite(5, 255);
            break;

            case 3: //TV
              io.analogWrite(6, 255);
            break;

            case 5: //MPLAY
              io.analogWrite(7, 255);
            break;
            
            case 6: //GAME
              io.analogWrite(12, 255);
            break;

            case 7: //TUNER
              io.analogWrite(13, 255);
            break;

            case 8: //AUX1
              io.analogWrite(14, 255);
            break;

            default:
            break;
            
          }
        break; 

        case 4: //Video source
          sr5010MapLocalStateGetText(cmdIndices[field], false, strOut);
          M5.Lcd.printf("Video: %s", strOut);
        break;

        /*
        case 7: //Zone 2
          sr5010MapLocalStateGetText(cmdIndices[field], false, strOut);
          M5.Lcd.printf("Zone2: %s", strOut);
        break;
        */

        default:
        break;
      }
    }
  }
}

int sendPreset(byte row, byte col)
{
    int indexBase = (row*4) + col;
    
    if(avPresets[indexBase].audioSrc != -1)
    {
      //Poke SI
      sr5010PokeState(4, avPresets[indexBase].audioSrc);
    }

    if(avPresets[indexBase].videoSrc != -1)
    {
      //Poke SV
      sr5010PokeState(5, avPresets[indexBase].videoSrc);
    }
  
  return 0;
}

int sr5010MapLocalStateGetText(char charIndex, bool isInteger, char* pStrOut)
{
  char argIndex = 0;
  
  if(pStrOut == nullptr) return 1;
  
  if(!isInteger)
  {
    //find argument string major index
    for(char i = 0; i < charIndex; i++)
    {
      argIndex += sr5010Map[i].numArgs; //get characteristic argument list offset
    }
    argIndex += sr5010MapLocalState[charIndex]; //add characteristic value to index
  
    strcpy(pStrOut, sr5010MapArgsOut[argIndex]);
  }
  else
  {
    sprintf(pStrOut, "%d\0", sr5010MapLocalState[charIndex]);  
  }

  return 0;
}

int sr5010PokeState(char stateIndex, int stateValue)
{
  char strOut[9];
  BLERemoteCharacteristic* pRemoteCharacteristic;

  if(pRemoteService != nullptr)
  {
    //Poke remote characteristic
    pRemoteCharacteristic = pRemoteService->getCharacteristic(sr5010Map[stateIndex].uuid);
    if (pRemoteCharacteristic != nullptr)
    {
      Serial.printf("-> %s: ", sr5010Map[stateIndex].uuid);
        
      //Write value to remote characteristic.
      if(pRemoteCharacteristic->canWrite()) 
      {
        sprintf(strOut, "%d\0", stateValue);
        pRemoteCharacteristic->writeValue(strOut);
        Serial.println(strOut);
      }
      else return 2;
    }
    else return 1;
  }

  return 0;
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
  //NOTE: Something in the M5Stack API makes BLE init unreliable.
  M5.begin(true, false, false, true);
  /*
    Power chip connected to gpio21, gpio22, I2C device
    Set battery charging voltage and current
    If used battery, please call this function in your project
  */
  M5.Power.begin();
  M5.Lcd.setBrightness(200);
  M5.Lcd.setTextWrap(true, true);

  //turn off M5Stack speaker clicking noise.
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
  Serial.println("Started LCD.");
  M5.Lcd.println("Started LCD.");
  M5.Lcd.printf("Battery at %u%%.\n", batteryLevel);
  M5.update();
  
  Serial.begin(115200);

  doConnect = true;
  Serial.println("Starting BLE.");
  M5.Lcd.println("Starting BLE.");
  M5.update();
  BLEDevice::init("KnobSwitch-Ctrl");

  updateAll = true; //pull all remote characteristics at connection time.

  if (io.begin(SX1509_ADDRESS))
  {
    //io.keypad(KEY_ROWS, KEY_COLS, sleepTime, scanTime, debounceTime);
    io.keypad(4, 4, 256, 32, 16);
    io.pinMode(4, ANALOG_OUTPUT);
    io.pinMode(5, ANALOG_OUTPUT);
    io.pinMode(6, ANALOG_OUTPUT);
    io.pinMode(7, ANALOG_OUTPUT);
    io.pinMode(12, ANALOG_OUTPUT);
    io.pinMode(13, ANALOG_OUTPUT);
    io.pinMode(14, ANALOG_OUTPUT);
    io.pinMode(15, ANALOG_OUTPUT);
    pinMode(36, INPUT_PULLUP); //SX1509 INT*
  }
  else Serial.println("SX1509 not found");


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
  
  char i;
  if(connected)
  {
    
    if(millis() >= time_now2 + 100) //every 100ms
    {
      //get all button input states at once.
      
      i = rotary.rotate() | rotary.push() << 2 | (~digitalRead(39) << 3) | \
      (~digitalRead(38) << 4) | (~digitalRead(37) << 5) ;
    
      if(!digitalRead(35))
      {
        unsigned int keyData = io.readKeypad();
      
        if (keyData != 0) // If a key was pressed:
        {
          byte row = io.getRow(keyData);
          byte col = io.getCol(keyData);
          Serial.printf("Key (%d, %d)\n", row, col);
          keyData = sendPreset(row, col);
          if(keyData) Serial.printf("sendPreset() = %d\n", keyData);

          i |= (1 << 6);
        }
      }
  
      time_now2 = millis();
    }
  
    if( i != UIStatePrevious)
    {
      if(sleepTimerExpired)
      {
        //wake up LCD, reconnect to BLE server and process UI input.
        //M5.Lcd.wakeup();
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

    //poke remote characteristics per the control being operated.
    char c;
    char txBuf[16];
    std::string val;

    BLERemoteCharacteristic* pRemoteCharacteristic;

    if(!digitalRead(39)) //button A
    {
      sr5010MapLocalState[1] ^= 1; //toggle power state.
      
      //Poke ZM.
      sr5010PokeState(1, sr5010MapLocalState[1]);

      while(!digitalRead(39)); 
    }

    i = rotary.rotate(); // 0 = not turning, 1 = CW, 2 = CCW
    if(i) //rotary encoder rotation
    {
      //Poke MV up/down.
      sr5010PokeState(9, (i-1));
    }

    if(rotary.push()) //rotary encoder button.
    {
      sr5010MapLocalState[3] ^= 1; //toggle mute state.
      
      //Poke MU.
      sr5010PokeState(3, sr5010MapLocalState[3]);
    }

    if(!digitalRead(38)) //button B
    {
      time_now = millis();
      sleepTimerExpired = false;
      
      sr5010MapLocalState[4]++; //increment input state.
      if(sr5010MapLocalState[4] >= sr5010Map[4].numArgs) sr5010MapLocalState[4] = 0;
      
      c = '0' + sr5010MapLocalState[4];
      
      //Poke SI
      sr5010PokeState(4, sr5010MapLocalState[4]);

      while(!digitalRead(38)); 
    }

    if(!digitalRead(37)) //button C
    {
      time_now = millis();
      sleepTimerExpired = false;
      
      sr5010MapLocalState[5]++; //increment video input state.
      if(sr5010MapLocalState[5] >= sr5010Map[5].numArgs) sr5010MapLocalState[5] = 0;
      
      //Poke SV
      sr5010PokeState(5, sr5010MapLocalState[5]);

      while(!digitalRead(37)); 
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
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }

  if(!sleepTimerExpired && (millis() - time_now >= 10000))
  {
    sleepTimerExpired = true;
    Serial.println("Sleeping now.");
    M5.Lcd.setBrightness(0);
    //M5.Lcd.sleep();
  }

  if(updateBitfield)
  {
    UIUpdate(updateBitfield);
    updateBitfield = 0;
    updateLcd = true;
  }

  if(updateLcd)
  {
    M5.update();
    updateLcd = false;
  }

} // End of loop

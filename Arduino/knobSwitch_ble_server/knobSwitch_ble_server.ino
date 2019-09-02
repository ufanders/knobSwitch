/*
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleServer.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updates by chegewara
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string.h>
#include <stdio.h>

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

//TODO: refactor into struct to map (statusType, UUID).

struct sr8500_characteristic {
  char uuid[37];
  char statusStr[4];
};

sr8500_characteristic sr8500CharMap[] = {
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
  {"5731a6c9-64c6-41ee-9fd3-0cc3d0bd7692", "MTM"}
};

#define SR8500_NUMCHARS (sizeof(sr8500CharMap)/sizeof(sr8500_characteristic))

/*
enum SR8500_chars_idx {
  pwr, vol, audio_mute, tone_bass, tone_treb, input_src, input_7p1_en,
  ad_input, audio_att, surround_mode, thx, ab_speaker, tuner_freq, tuner_preset, tuner_mode,
  tuner_keys, menu_keys, test_tone, ch_level, dolby_hp_mode, night_mode, sleep_timer, multiroom_en,
  multiroom_vol, multiroom_mute, multiroom_spkr, multiroom_spkr_vol, multiroom_spkr_mute, multiroom_src, multiroom_tuner_freq, multiroom_tuner_preset,
  multiroom_tuner_mode, ht_eq, signal_fmt, sampling_freq, channel_stat,
  SR8500_chars_idx_max
};

const char SR8500_chars_uuid[SR8500_chars_idx_max][37] = {
  "7b0819db-21f6-429d-ad5a-c31215fc7114",
  "1ffacfe7-6051-42b1-8dab-004e164a82cd",
  "d1cf0f01-a78d-4de0-9812-bd391bcba635",
  "18a36e61-32fb-4f9d-932f-75c246f1f881",
  "01f55e00-5b3b-443f-8ca2-24f2b3ca72fb",
  "d33efca9-e30a-4e41-98fc-d5c84e2b3f77",
  "d878b661-6cae-48e8-a84c-3952f89f5623",
  "25f76a7b-87e3-498e-9abf-290fb89c16c7",
  "c7d89e80-42b2-4b0e-8b6b-a783258e48e5",
  "b7dae477-dd0d-4f0e-afdb-229e3bdb983a",
  "6ac062d9-b0a2-4483-800e-a6497c7365ab",
  "f4544ed7-17fa-4d7d-a826-8513132eb31b",
  "15095cae-1aca-423b-ba62-609f8bf6c0ac",
  "a89367a0-9e05-461d-9a07-10b8135f3a46",
  "7e8f0681-a312-4934-9300-65de924e4ff0",
  "ef8ce36d-1e04-4d4a-845d-d71707201257",
  "5e30b0bc-d3a3-45a6-a95b-ad5bfb6647a5",
  "1bf5ef56-90d4-46d5-87b8-35c6d071a01b",
  "eeea6f37-db29-44d0-bd4d-7c45641fa92f",
  "f46b3732-cd2c-4a3d-8301-1b7167d7fd54",
  "6535a6f2-961a-4f7c-af67-6192be58bd8e",
  "b276067f-6e05-44ef-8231-2853a8f6a405",
  "763cf4bc-e298-4fe1-b70a-f2029ff21f8d",
  "fb7478cf-7f59-4056-9796-ae9768956b4e",
  "7fb93eb2-ed8f-4051-9497-15feb0e0db84",
  "58110e30-5916-4c04-a71e-729ccd58147b",
  "ee4b9c81-d628-415d-9fd2-b53e3bb2280b",
  "93521329-e117-41c7-97c7-e3cd466457e7",
  "1ecd0948-e5f9-402c-b342-611417ccf8cc",
  "dd3361c4-a86a-42f3-8d77-8caaad5ded17",
  "f1b68440-406c-4c0b-8b52-921947143d2b",
  "d72a2014-1cc1-4fbc-ba51-ec07dc4c4af4",
  "c67b1a61-1ac9-43b0-a5c1-8228ab12effd",
  "34eba641-6f2e-446a-8755-ea1d0fd30d62",
  "7bb61d7e-5cac-4b90-8982-1972975b12b9",
  "5731a6c9-64c6-41ee-9fd3-0cc3d0bd7692"
};

const char SR8500_statusTypes[][4] ={
  "PWR", "VOL", "AMT", "TOB", "TOT", "SRC", "71C", "INP",
  "ATT", "SUR", "THX", "SPK", "TFQ", "TPR", "TMD", "MNU",
  "TTO", "CHL", "NGT", "REQ", "SLP", "SIG", "SFQ", "CHS",
  "MPW", "MVL", "MAM", "MVS", "MSP", "MSV", "MSM", "MST",
  "MSC", "MTF", "MTP", "MTM"
};
*/

//BLECharacteristic* sr8500_chars_ptr[SR8500_chars_idx_max];
BLECharacteristic* sr8500_chars_ptr[SR8500_NUMCHARS];

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE work!");

  Serial1.begin(9600, SERIAL_8N1, 5, 4); //4=TXD, 5=RXD.

  BLEDevice::init("Long name works now");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic;
  
  for(int i = 0; i<SR8500_NUMCHARS-4; i++)
  {
    //pCharacteristic = pService->createCharacteristic(SR8500_chars_uuid[i],
    sr8500_chars_ptr[i] = pService->createCharacteristic(sr8500CharMap[i].uuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  }

  for(int i = SR8500_NUMCHARS-4; i<SR8500_NUMCHARS; i++)
  {
    //pCharacteristic = pService->createCharacteristic(SR8500_chars_uuid[i],
    sr8500_chars_ptr[i] = pService->createCharacteristic(sr8500CharMap[i].uuid,
    BLECharacteristic::PROPERTY_READ);
  }

  //TODO: read/refresh all statuses.
  
  pCharacteristic->setValue("Hello World says Neil");
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristics defined! Now you can read it in your phone!");
}

char rxBuf[32], rxBufIdx, rxBufLen;

void loop() {

  char rc;

  //TODO: receive any new status update.
  if(Serial2.available())
  {
    rxBufIdx = 0; rxBufLen = 0;
    
    //read in characters until we see a LF.
    while(Serial2.available())
    {
      rc = Serial2.read();
      if(rc != '\r') rxBuf[rxBufIdx++] = rc; 
      else rxBuf[rxBufIdx] = '\0'; rxBufLen = rxBufIdx;
    }
  }

  if(rxBufLen && rxBufIdx) processUpdateSerial(rxBuf);
  
}

int processUpdateSerial(char* strUpdate)
{
  //TODO: parse update string and update corresponding characteristic value.
  char statusType[4]; //includes null terminator.
  scanf("%3s", statusType); //read first 3 characters.

  //search for the characteristic the received status update corresponds to.
  int i = 0;
  while(i<SR8500_NUMCHARS)
  {
    if(!memcmp(statusType, sr8500CharMap[i].statusStr, 3))
    {
      //TODO: update characteristic value with received status value.
      
      break;
    }
  }
  
  return 0;
}

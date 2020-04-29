#ifndef _KNOBSWITCH_H_
#define _KNOBSWITCH_H_

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "18550d7d-d1aa-4968-a563-e8ebeb4840ea"

//NOTE: The STANDBY MODE must be set to NORMAL 
//instead of ECONOMY for the power function to work correctly.

#define BIT_ISUPDOWN 0b1

struct sr5010_property_map {
  char uuid[37];
  char statusStr[9]; //up to 8-character prefix (wtf!)
  int numArgs;
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
  "CD", "DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "TUNER", /*"IRADIO", "SERVER", "FAVORITES",*/ "AUX1", "AUX2", /*"INET",*/ "BT", "USB/IPOD", 
  "DVD", "BD", "TV", "SAT/CBL", "MPLAY", "GAME", "AUX1", "AUX2", "CD", "OFF",
  "ON", "OFF" //Z2
};

#define SR5010_NUMCHARS (sizeof(sr5010Map)/sizeof(sr5010_property_map))

char sr5010_searchByUUID(const char*);
char sr5010_searchByCmd(char*);

#endif

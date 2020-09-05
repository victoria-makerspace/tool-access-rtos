#include <MFRC522.h>
#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncMqttClient.h>
#include "tool-access-RTOS.h"
#include "credentials.h"

// MFRC522 Instantiation
MFRC522 mfrc522(SS_PIN, RST_PIN);

// SPIFFS Instantiation
File file;

// Creat FastLED instance
CRGB leds[NUM_LEDS]; // We have 2 LEDs and we count from 0


void toolAccessInit () {

  //GPIO Config
  pinMode(RELAY_PIN, OUTPUT);   // Set relay pin for output
  pinMode(LEDATA_PIN, OUTPUT); // Set LED pin for output

  Serial.begin(115200);   // Initialize serial communications with the PC
  SPI.begin();      // Init SPI bus

  //RFID Setup
  mfrc522.PCD_Init();   // Init MFRC522

  // For debugging purposes - lets us know if getting coms from RFID module
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
  Serial.println(F("Scan PICC to see UID, SAK, type, and data blocks..."));

  // LED Init
  FastLED.addLeds<WS2812, LEDATA_PIN, RGB>(leds, NUM_LEDS);
  LEDS.setBrightness(50); // Set LED brightness to 50%

  // SPIFFS Init
  // Mount SPIFFS file system
  if (!SPIFFS.begin(true)){ // Passing true will format SPIFFS on first interaction?
    Serial.println("An Error has occured SPIFFS during mount");
      return;
  }

}

/////////////////////////////////////////  RFID Functions   ///////////////////////////////////

/* Redundancy - UID handling
Have facility to copy uid as byte from mfrc522 struct to our own
Have facility to convert uid from byte to string
Need not do both but which depends on approach
If sticking with MQTT may as well convert directly to string
If going with something else may make more sense to use byte
*/ 
/* Reads UID out of mfrc522.uid.uidByte struct, prints to serial and into array
   Card MUST have already been read by PICC_Select () OR PICC_ReadCardSerial ()
*/
uint8_t userID(metaStruct *progParams, byte value[], byte valueSize) {
  // Prog struct , mfrc522 struct
  //byte buffer[], byte *sizeBuff,
  //metaStruct->card

  progParams->card.uid_length = valueSize; 
  Serial.print(F("Card UID:"));
  for (byte i = 0; i < progParams->card.uid_length; i++) {
    if (value[i] < 0x10) //
      Serial.print(F(" 0"));
    else
      Serial.print(F(" "));
    progParams->card.uid_buffer[i] = value[i];
    //Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(progParams->card.uid_buffer[i], HEX);
  }

  Serial.println(); // print a space
  mfrc522.PICC_HaltA(); // Place card in halt state
}


/* Checks for continued presence of previously read (ie. Halted) card.
    Polling frequency is controlled externally using the isitTime ()
    Current implementation means that there is a 0-10s timer on when the ESP reacts to the removal of the card.
    How this works:
    PICC_WakeupA (byte *bufferATQA, byte *bufferSize), see MFRC522.cpp lines 568
    WakeupA returns STATUS_OK = 0 (see MFRC522.h lines 307-317 for StatusCode enums) on successful wakeup
    Otherwise STATUS_?? on unsuccessful
*/

extern void collPolling (metaStruct *progParams, TaskHandle_t *blinkLEDHandle0, TaskHandle_t *blinkLEDHandle1, EventGroupHandle_t *rfidStatesGroup, TaskHandle_t *pollNewHandle){
// When an additional card has entered the field in addition to a previously halted card repeated calls of 
// IsNewCard will produce a pattern of 0101 telling us we have a collision.

for (int i = 0; i < 4; i++){ 
  Serial.println("--");
  if (mfrc522.PICC_IsNewCardPresent()){ // Any more than 1 and we have a collision or a misaligned card
    //inc counter
  //++(progParams->card.collCounter);
  (progParams->card.collCounter)++;
  }
  else{
    // Do nothing
  }
}
// Note on the counter: for the life of me I can't figure out what I can't make the counter a member of the card Struct and have this function behave properly
  if (progParams->card.collCounter >= 2){
    // We have a collision
    // GOTO state TIMEOUT
    xEventGroupClearBits(*rfidStatesGroup, CARD_BIT_0|AUTH_BIT_1); // Revoke card and authorization bits
    Serial.println("Collision!");

    // Blink purple lights to indicated collision detected
    progParams->LEDParams0.myColour = CRGB::Purple;
    progParams->LEDParams1.myColour = CRGB::Purple;
    progParams->LEDParams0.blink = 1;
    progParams->LEDParams1.blink = 1;
    
    // Resume our LED blinking
    vTaskResume(*blinkLEDHandle0); 
    vTaskResume(*blinkLEDHandle1);
    
    //mfrc522.PICC_HaltA(); // Halt the offending cards
    //mfrc522.PICC_HaltA(); // Halt the offending cards

    //vTaskResume(*pollNewHandle);

    // This should be last so that the Timeout Task doesn't unblock until we're done twidling flags and LEDParams
    xEventGroupSetBits(*rfidStatesGroup, TIMEOUT_BIT_3|COLL_BIT_4); // Set Timeout and collision bits as last action as this unblocks the timeout task
  }
  else{ // No collision
    Serial.println("No collision");
    
    // This could possibly be an issue
    //xEventGroupClearBits(*rfidStatesGroup, COLL_BIT_4); // We can revoke the collision flag if it was previously set because the offending card has been removed
  }
progParams->card.collCounter = 0;
}

/* Polls the RFID card read for the continued presence of a card.
It works based on only Halted cards responded to the WakeupA () command
*/
void pollPres (metaStruct *progParams, EventGroupHandle_t *rfidStatesGroup, TaskHandle_t *blinkLEDHandle0, TaskHandle_t *blinkLEDHandle1, TaskHandle_t *pollNewHandle) {

  Serial.println("Welcome to presence polling");

  EventBits_t uxBits;
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  byte resultWake;
  byte resultReq;

// Reset baud rates
	mfrc522.PCD_WriteRegister(mfrc522.TxModeReg, 0x00);
	mfrc522.PCD_WriteRegister(mfrc522.RxModeReg, 0x00);
	// Reset ModWidthReg
	mfrc522.PCD_WriteRegister(mfrc522.ModWidthReg, 0x26);

  resultWake = mfrc522.PICC_WakeupA (bufferATQA, &bufferSize);
 
  // Old implenetation - is broken by collisions, therefore must ensure it only occurs in state1Card
  
  if (resultWake == 0) { // STATUS_OK == 0 tells us our card is still there
    mfrc522.PICC_HaltA(); // Re-halt the card after successful Wakeup otherwise we will unnecessarily reread the UID
  }
  else{ //Did not recieve STATUS_OK from WakeupA therefore the card is gone
    
    //*cardFlag = 0; // Change the flag state so that polling for continue card presence will cease
    xEventGroupClearBits(*rfidStatesGroup, CARD_BIT_0 | AUTH_BIT_1); // Clear the card and auth bit
    //vTaskResume()

    //Creat blink task?
    // Set both LEDS to Green
    progParams->LEDParams0.myColour = CRGB::Blue; 
    progParams->LEDParams1.myColour = CRGB::Blue;
    // Set both LEDs to not blink
    progParams->LEDParams0.blink = 1;
    progParams->LEDParams1.blink = 1;
    // Now resume the blink tasks
    vTaskResume(*blinkLEDHandle0); 
    vTaskResume(*blinkLEDHandle1);
    
    // Initiate timeout via setting TIMEOUT_BIT_3 to unblock timeout task
    xEventGroupSetBits(*rfidStatesGroup, TIMEOUT_BIT_3);
    
    //vTaskResume(*pollNewHandle);
    
    // Is this actually needed?
    mfrc522.PICC_WakeupA (bufferATQA, &bufferSize); // This is needed incase a second card was introduced creating a false timeout. Should force reauth on exit?
  }
  
}

/////////////////////////////////////////  Data Wrangling Functions   ///////////////////////////////////

/* Converts a byte array to an integer. Takes array and a starting position.
   NVS does not have good provisioning for storage of arrays so converting to an integer seems easiest.
*/
int arraytoInt32 (byte UIDarray [], int pos) {
  int result = 0;
  result |= UIDarray [pos++] << 24;
  result |= UIDarray [pos++] << 16;
  result |= UIDarray [pos++] << 8;
  result |= UIDarray [pos];
            return result;
}

void byteToHexStr(byte * in, size_t insz, char * out, size_t outsz){
  byte * pIn = in;
  char * pOut = out;
  const char * hex = "0123456789ABCDEF";

  for (; pIn < in + insz; pOut += 3, pIn++) {
    pOut[0] = hex[(*pIn >> 4) & 0xF];
    //Serial.println(pOut[0]);
    pOut[1] = hex[ *pIn     & 0xF];
    //Serial.println(pOut[0]);
    pOut[2] = ':';
    if (pOut + 3 - out > outsz) {
      /* Better to truncate output string than overflow buffer */
      /* it would be still better to either return a status */
      /* or ensure the target buffer is large enough and it never happen */
      break;
    }
  }
  pOut[-1] = 0;
}

/////////////////////////////////////////  SPIFFS Functions   ///////////////////////////////////

// Separate SPIFFS init function?
// May not want to mount SPIFFS unless necessary

void writeLog (metaStruct *progParams){

// deleteFile(SPIFFS, "/test.txt");

// Debugging 
//listDir(SPIFFS, "/", 0); // Print directory list to Serial

// Check for log file
// If exists append
// Check if log file exists
if (SPIFFS.exists("/uidLogs")){ // Log file exists
  appendFile (SPIFFS, "/uidLogs", progParams->card.uidStr);
}
// If doesn't exist then write
else if (!SPIFFS.exists("/uidLogs")){ // Log file does not exist
  writeFile (SPIFFS, "/uidLogs", progParams->card.uidStr); // Create log file and open file in write mode (we need not worry about overwriting)
}

// testFileIO (SPIFFS, "/uidLogs.txt");
readFile(SPIFFS, "/uidLogs.txt"); // Read out the log you just wrote (function writes to Serial)


// Read file for debugging purposes

file.close();

}


// Very helpful code for interacting with SPIFFS https://gist.github.com/xxlukas42/fc0135639fcc1fc61c8b2a25649be59d
// Originally found via youtube: https://www.youtube.com/watch?reload=9&v=v-7TI3kWntw
// All code below this point is modified from the arduino SPIFFS sketch on xxlukas42's github

/* Lists "directory" (flat) in SPIFFS partition 
* Ex call: listDir (SPIFFS, "/", 0), the SPIFFS object (belongs to SPIFFSFS class) is instantiated in SPIFFS.cpp && externalized in SPIFFS.h
* Expects address of fs object, string of directory name, and # of directory levels (always 0?)
*/ 
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){ 
    Serial.printf("Listing directory: %s\r\n", dirname);

// Class File's constructor lives in FS.h
// fs.open () is a factory [an object that creates other objects]
// fs.open (dirname) returns a File object configured to access dirname. In this case the object is named root
// NOTE: fs.open () has default arg of FILE_READ (ie. this arg is applied unless otherwise specified) therefore our root object is in read mode
    File root = fs.open(dirname); // tl;dr: Open the directory at specified path (in this case root [but reallyL SPIFFS/])
    if(!root){ // If our root object of class File did not instantiate we have a problem
        Serial.println("- failed to open directory"); // Report that problem to Serial
        return;
    }
    if(!root.isDirectory()){ // If our root object of class File doesn't have a directory
        Serial.println(" - not a directory"); // Then report that there isn't a directory
        return;
    }

/*
*/
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

/* Read a file. Expects address of SPIFFS object, and string of path to file to be read
* Note: FILE_WRITE is #define in FS.h to w, likewise FILE_APPEND = a, and FILE_READ = r
*/
void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\r\n", path); // Print to Serial what file we are reading %s - string 

// We create a different object for reading 
// NOTE: fs.open () has default arg of FILE_READ (ie. this arg is applied unless otherwise specified)
    File file = fs.open(path); // Open the file at given path 
    if(!file || file.isDirectory()){ // 
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while(file.available()){
       // Serial.write(file.read());
      Serial.println(file.read());
    }
}

/* Read a file. Expects address of SPIFFS object, and string of path to file to be read
* Note: FILE_WRITE is #define in FS.h to w, likewise FILE_APPEND = a, and FILE_READ = r
*/
void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

// NOTE: Here we supply an arg OTHER THAN default inorder to enter FILE_WRITE
    File file = fs.open(path, FILE_WRITE); // FILE_WRITE mode will overwrite file contents that previously existed!
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("- file written");
    } else {
        Serial.println("- frite failed");
    }
}

void writeFile(fs::FS &fs, const char * path, int number, int base){
    Serial.printf("Writing file: %s\r\n", path);

// NOTE: Here we supply an arg OTHER THAN default inorder to enter FILE_WRITE
    File file = fs.open(path, FILE_WRITE); // FILE_WRITE mode will overwrite file contents that previously existed!
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(number, base)){
        Serial.println("- file written");
    } else {
        Serial.println("- frite failed");
    }
}

/* Append to passed file. Expects address of SPIFFS object, string of path to file, and text to append to file
*/
void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\r\n", path);

// NOTE: Here we pass FILE_APPEND
    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message)){ // Report on success or fail of append
    file.print(","); // CSV your data 
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
}

/* Overload for appending an int as Hex 
*/
void appendFile(fs::FS &fs, const char * path, int number, int base){
    Serial.printf("Appending to file: %s\r\n", path);

// NOTE: Here we pass FILE_APPEND
    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(number, base)){ // Report on success or fail of append
    file.print(","); // CSV your data 
        Serial.printf("%X - message appended", number);
    } else {
        Serial.println("- append failed");
    }
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("- file renamed");
    } else {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\r\n", path);
    if(fs.remove(path)){
        Serial.println("- file deleted");
    } else {
        Serial.println("- delete failed");
    }
}

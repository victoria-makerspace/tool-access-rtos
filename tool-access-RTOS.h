#ifndef tool-access-RTOS_H    // Put these two lines at the top of your file.
#define tool-access-RTOS_H    // (Use a suitable name, usually based on the file name.)

// Hardware defines
#define RST_PIN         22          // Configurable, see typical pin layout above
#define SS_PIN          21         // Configurable, see typical pin layout above
#define RELAY_PIN       17         // GPIO pin wired to a BC337 transistor that triggers relay coil
#define LEDATA_PIN 32             // WS2812 LEDs data pin is wired to pin 32 through a 330Ohm resistor
#define NUM_LEDS 2                // # of LEDs in our daisy chain

//Timing defines
#define MS_WIFI_RECONNECT_PERIOD pdMS_TO_TICKS(2000) // Wifi reconnect time in ms converted to RTOS ticks
#define MS_MQTT_RECONNECT_PERIOD pdMS_TO_TICKS(2000) // MQTT reconnect time in ms converted to RTOS ticks
#define MS_POLL_TIMER_PERIOD pdMS_TO_TICKS(1000) // RFID module polling frequency converted to RTOS ticks
#define MS_TIMEOUT_PERIOD pdMS_TO_TICKS(60000) // ms_timeOut after card removal
#define COLL_TIMEOUT_PERIOD pdMS_TO_TICKS(2000) // timeout when collision is detected
#define LEDBLINK_PERIOD pdMS_TO_TICKS(10000) // Blink period for temporary blinking

// MQTT defines
//#define MQTT_HOST IPAddress(10, 1, 2, 123)
#define MQTT_HOST IPAddress(192, 168, 1, 26)
#define MQTT_PORT 1883

// Event group macros
#define CARD_BIT_0 ( 1 << 0 )         // 1's
#define AUTH_BIT_1 ( 1 << 1 )         // 2's
#define RELAY_BIT_2 ( 1 << 2 )        // 4's
#define TIMEOUT_BIT_3 ( 1 << 3 )      // 8's
#define COLL_BIT_4 ( 1 << 4 )         // 16's
#define ESTOPFIRE_BIT_5 ( 1 << 5 )    // 32's
#define ESTOPCLEAR_BIT_6 ( 1 << 6 )   // 64's
#define WIFIOUT_BIT_7 ( 1 << 7 )      // 128's

// MFRC522 instantiation
extern MFRC522 mfrc522;

//SPIFFS instantiation
extern File file;

// CRGB/FastLED instantiation
extern CRGB leds[];

// Structs
typedef struct {       // Struct for data to be logged
  // These may be depreciated
  byte uid_buffer[31]; // Holds UID read out of MFRC522 library
  byte uid_length;     // Holds size of uid_buffer
  // Holds the string version of the uid byte living in the mfrc522 struct
  size_t uidStrLen; // Holds the length (not size) of uidStr
  char uidStr[31]; // Biggest possible UID is 10bytes * 3 (because we : separate, eg. 0xFF:etc) + 1 (NULL) = 31
  // Counter used in detecting card collisions 
  char collCounter;     
}cardParams;

typedef struct{ // Stuct for passing desired LED params
  int led;            // Which LED
  int blinkPeriod;    // Period of the LED blinking, specified in milliseconds
  bool blink;         // To blink or not to blink?
  bool blinkTemp;     // Do we want our blink to be temporary
  CRGB myColour;      // What colour to be
} LEDParams;

typedef struct{
  LEDParams LEDParams0; // Parameters for LED0
  LEDParams LEDParams1;  // Parameters for LED1
  cardParams card;      // Holds info about read card
} metaStruct;


//Initialization
extern void toolAccessInit (); // Carries out initialization of various peripherals and WiFi

//RFID Functions
//extern uint8_t userID(byte buffer[], byte *size, byte value[], byte sizeBuff); // Prints out UID stored in mfrc522.uid.uid struct. Card must have been read by PICC_Select OR PICC_ReadCardSerial
extern uint8_t userID(metaStruct *progParams, byte value[], byte sizeBuff); // Prints out UID stored in mfrc522.uid.uid struct. Card must have been read by PICC_Select OR PICC_ReadCardSerial
extern void pollPres (metaStruct *progParams, EventGroupHandle_t *rfidStatesGroup, TaskHandle_t *blinkLEDHandle0, TaskHandle_t *blinkLEDHandle1, TaskHandle_t *pollNewHandle); // Polls for continued card presence at reader
extern void collPolling (metaStruct *progParams, TaskHandle_t *blinkLEDHandle0, TaskHandle_t *blinkLEDHandle1, EventGroupHandle_t *rfidStatesGroup, TaskHandle_t *pollNewHandle);
extern bool isitTime (uint32_t *timeNow, uint32_t *timeLast, uint32_t interval); // Returns boolean for if a time interval has elapsed
extern bool checkTwo ( byte a[], byte b[]); // Compares two byte arrays and returns result as bool
extern bool isAllowed( byte test[]); 

//LED functions
extern void LEDInit ();
extern void green ();
extern void LEDOff ();
extern void redwBlink ();
extern void purpleBlink();

//Data wrangling functions
extern int arraytoInt32 (byte UIDarray [], int pos); // Converts 4 byte array to an integer 
extern void structInit ();
extern void structReset ();
void byteToHexStr(byte * in, size_t insz, char * out, size_t outsz);

//SPIFFS Functions
extern void writeLog (metaStruct *progParams); // Take pointer to interger of UID previous read and writes it to uigLog.txt in SPIFFS partion
extern void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
extern void readFile(fs::FS &fs, const char * path);
extern void writeFile(fs::FS &fs, const char * path, const char * message);
extern void writeFile(fs::FS &fs, const char * path, int number, int base);
extern void appendFile(fs::FS &fs, const char * path, const char * message);
extern void appendFile(fs::FS &fs, const char * path, const int number, int base);
extern void renameFile(fs::FS &fs, const char * path1, const char * path2);
extern void deleteFile(fs::FS &fs, const char * path);
#endif // tool-access-RTOS_H    // Put this line at the end of your file.

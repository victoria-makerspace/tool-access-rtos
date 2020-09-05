#include <MFRC522.h>
#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncMqttClient.h>
#include "tool-access-RTOS.h"
#include "credentials.h"

AsyncMqttClient mqttClient;

// Task Handlers
TaskHandle_t blinkLEDHandle0;
TaskHandle_t blinkLEDHandle1;
TaskHandle_t basicTaskHandle;
TaskHandle_t closeRelayHandle;
TaskHandle_t pollPresHandle;
TaskHandle_t pollNewHandle;
TaskHandle_t collPollHandle;
TaskHandle_t timeoutHandle;
TaskHandle_t eStopFireHandle;
TaskHandle_t eStopClearHandle;

// Timer Handlers
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;
TimerHandle_t LEDblinkTimer;

// Event Group Handlers
EventGroupHandle_t rfidStatesGroup;

// Semaphore/Mutex Handlers
SemaphoreHandle_t SPIMutexHandle;


/////////////////////////////////////////  WiFi Tasks   ///////////////////////////////////

 void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  
  //Correct credentials
  WiFi.begin(SSID, PASS); // Must create own credentials.h with appropriate #defines for SSID and PASS

  // Incorrect creds for testing  purposes
  // WiFi.begin(SSID, PASS)bad;
}


void connectToMqtt() {
  Serial.println("Connecting to MQTT...");  
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        connectToMqtt();
        // Clear the WiFi outage bit
        xEventGroupClearBits(rfidStatesGroup, WIFIOUT_BIT_7);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		    xTimerStart(wifiReconnectTimer, 0); // Start wifiReconnectTimer immediately

        // Set WiFi outage bit to change our authorization scheme
        xEventGroupSetBits(rfidStatesGroup, WIFIOUT_BIT_7);
        break;
    }
}

/////////////////////////////////////////  MQTT Tasks   ///////////////////////////////////

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  xEventGroupClearBits(rfidStatesGroup, WIFIOUT_BIT_7); // We've established connection to MQTT now clear the outtage bit

  // Sub to the rfid topic
  uint16_t packetIdSub2 = mqttClient.subscribe("rfid/auth/rsp", 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub2);

  // Sub to the estop topic
  uint16_t packetIdSub3 = mqttClient.subscribe("rfid/estop", 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub3);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  xEventGroupSetBits(rfidStatesGroup, WIFIOUT_BIT_7); // Can't connect to MQTT, set the outage bit to change auth scheme
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0); // We're back on the WiFi time to start trying to reconnect to MQTT broker
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {

  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);

  int topicLength = (strlen(topic));
  int payloadLength = (strlen(payload));
  const char * rsp = "rfid/auth/rsp";
  const char * estop = "rfid/estop";

/* Possible pitfalls:
Retained messages will fall straight through into this on boot!!!
Be very careful how you use them.
*/

// refid/auth
    // This should maybe be functionized for neatness
    if ((strncmp (topic, rsp, topicLength)) == 0) // strncmp returns true if exact match
    {
      
      Serial.println("We have a matching topic");
      
      if (((strncmp (payload, "auth", 4)) == 0)){ // Read only four indices just incase the payload is not null terminated
        Serial.println("authorized!");
        xEventGroupSetBits(rfidStatesGroup, AUTH_BIT_1); //Set the authorized bit
        xEventGroupClearBits(rfidStatesGroup, TIMEOUT_BIT_3); // Clear the timeout bit just in case we're in a timeout
      }
      else if(((strncmp (payload, "denied", 6)) == 0)){
        Serial.println("denied!");
        xEventGroupClearBits(rfidStatesGroup, (CARD_BIT_0|AUTH_BIT_1)); // Revoke card and authorization bit if an unauthorized card arrives. 
        vTaskResume(pollNewHandle); // Resume polling for new cards
        
        // Test  
        xTaskNotify(pollNewHandle, (1<<0), eSetBits); // Set the 0th bit in the pollNew Handle
      
      
      
      }
      else if(((strncmp (payload, "seekiosk", 8)) == 0)){
        Serial.println("seekiosk!");
        // Set kiosk bit?
      }
    }
// rfid/estop
    else if ((strncmp (topic, estop, topicLength) == 0)){
      if (((strncmp (payload, "fire", 4)) == 0)){ // Read only four indices just incase the payload is not null terminated
        Serial.println("Fire the eStop!");
        xEventGroupSetBits(rfidStatesGroup, ESTOPFIRE_BIT_5); 
        
      }
      else if(((strncmp (payload, "clear", 5)) == 0)){
        Serial.println("Clear the eStop!");
        xEventGroupSetBits(rfidStatesGroup, ESTOPCLEAR_BIT_6); 
      }
    }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

  /////////////////////////////////////////  LED Task   ///////////////////////////////////

  /* Single method for RTOS task blinkLED  
  */
  void blinkyLED (void *params){
  UBaseType_t uxHighWaterMark;
  uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation


  Serial.print("blinkLED high water mark init:");
  Serial.println(uxHighWaterMark);


  LEDParams *l = (LEDParams*)params; // Dumping our struct parameters into task instance of LEDParams via casting of void *params to LEDParams

  
  for (;;){ // Infinite loop required for RTOS tasks b/c if allowed to return they would delete
    // Task code here
    uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.print("Blinky task: High watermark:");
    Serial.println(uxHighWaterMark);

    if(l->blink){ // if parameters specify blinking

      if (l->blinkTemp){
          l->blinkTemp=0; // Only want the timer started once
          xTimerStart(LEDblinkTimer, 0); // Start the timer
      }

      leds[l->led] = CRGB::Black; // Set LED specified in passed params to OFF state 
      FastLED.show(); // Toggle the LED state to new
      vTaskDelay(pdMS_TO_TICKS(l->blinkPeriod)); // RTOS delay blocks task not processor

      leds[l->led] = l->myColour; // Set LED specificed in passed params to the colour specified in same passed params
      //l->myColourl
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(l->blinkPeriod));; // Set delay time according to passed param AND div by port TICK period ms
    } 

    else{ 
      leds[l->led] = l->myColour; // Set LED specificed in passed params to the colour specified in same passed params
      FastLED.show(); // Show it 
      vTaskSuspend ( NULL ); // Suspend ourselves since blink = false the task need not keep running 
    }
  }
}

 /////////////////////////////////////////  Relay Task  ///////////////////////////////////

void closeRelayTask (void *params){
  
  metaStruct *progParams = (metaStruct*) params; // Should be static/const maybe?
  
  // UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
  
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (CARD_BIT_0 | AUTH_BIT_1); // This creates a mask for checking our bits. To make mask we always use |
  EventBits_t uxBits;                                          // But to check a mask we always use &

 // Serial.print("closeRelay high water mark init:");
 // Serial.println(uxHighWaterMark);
  
  for(;;){
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdFALSE, pdTRUE, xTicksToWait);
        
   // uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
   // Serial.print("closeRelay high water mark init:");
   // Serial.println(uxHighWaterMark);
    
    Serial.println("I'm going to close the relay!");
        
    // Set both LEDS to Green
    progParams->LEDParams0.myColour = CRGB::Green; 
    progParams->LEDParams1.myColour = CRGB::Green;
    // Set both LEDs to not blink
    progParams->LEDParams0.blink = 0;
    progParams->LEDParams1.blink = 0;
        
    Serial.println("Light the lights!");
    vTaskResume(blinkLEDHandle0); // Resume the LED task
    vTaskResume(blinkLEDHandle1); // Resume the LED task

        
    digitalWrite(RELAY_PIN, HIGH); // Close the relay

     // Set relayBit
    xEventGroupSetBits(rfidStatesGroup, RELAY_BIT_2);

    vTaskSuspend ( NULL ); // Suspend ourselves. No need to keep polling for new now that we know a card is there
   }
  }

 /////////////////////////////////////////  RFID Tasks   ///////////////////////////////////


void pollNewTask (void *params){
  metaStruct *progParams = (metaStruct*) params;
  uint32_t notificationValue;
  // UBaseType_t uxHighWaterMark;

  //uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
  //Serial.print("pollNew high watermark init:");
  //Serial.println(uxHighWaterMark);
  
  
  for(;;){
    //uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    
    vTaskDelay(MS_POLL_TIMER_PERIOD); // Wait for at least he polling time
    
    //Serial.print("pollNew high watermark task:");
    //Serial.println(uxHighWaterMark);
    
    notificationValue = ulTaskNotifyTake (pdTRUE, pdMS_TO_TICKS(300));
    if(notificationValue == 1 ){          // Notification from onMqttMessage that a card was denied
      Serial.println("Notification!!");
      Serial.println(notificationValue);
      // Set both LEDS to Red
      progParams->LEDParams0.myColour = CRGB::Red; 
      progParams->LEDParams1.myColour = CRGB::Red;
      // Set both LEDs to not blink
      progParams->LEDParams0.blink = 1;
      progParams->LEDParams1.blink = 1;
      progParams->LEDParams0.blinkTemp = 1; // We set blinkTemp because we only want to blink LEDs for short period of time
      progParams->LEDParams1.blinkTemp = 1; // That period is constrained by LEDblinkTimer and LEDBLINK_PERIOD
      vTaskResume(blinkLEDHandle0);
      vTaskResume(blinkLEDHandle1);
    }


    if(mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial ()){ // Poll for new cards BUT only when CARD_BIT_0 is not set

      /* Redundancy - UID handling
      Have facility to copy uid as byte from mfrc522 struct to our own
      Have facility to convert uid from byte to string
      Need not do both but which depends on approach
      If sticking with MQTT may as well convert directly to string
      If going with something else may make more sense to use byte
      */

      // Have removed copying of byte to our own struct for time being as prototype utilizes MQTT
      // userID(progParams, mfrc522.uid.uidByte, mfrc522.uid.size); // Move UID from struct to our own buffer
      
      mfrc522.PICC_HaltA(); // We have read the card now be halt it

      xEventGroupSetBits(rfidStatesGroup, CARD_BIT_0); // Now that we have detected a card set CARD_BIT_0 to unblock some Tasks

      /* Conditional check for WiFi/MQTT check needed here to determine if we need to log
      */

      // Now we need to check if the card's UID is authorized
      // But first we must convert that UID to a string

      // 2 byte = 2 hex digits
      // Size is bytes not length
      // 

      progParams->card.uidStrLen = (mfrc522.uid.size*3); // Our string length will be 3x the length the equivalent byte value
      
      byteToHexStr(mfrc522.uid.uidByte, mfrc522.uid.size, progParams->card.uidStr, progParams->card.uidStrLen); // Now convert the byte in the mfrc522 struct to a string and dump it into our struct string

      // Check if WiFi/MQTT is out
      // Bitmask to just the bits we care about: WIFIOUT_BIT_7
      if((xEventGroupGetBits(rfidStatesGroup) & 128) == 128){ // If the WiFi is out simply grant access and log
        Serial.println("WiFi is out");
        xEventGroupSetBits(rfidStatesGroup, AUTH_BIT_1); // If WiFi/MQTT server cannot be reached grant user access regardless of authorization
        // Code for logging should go here.
      }
      
      else{ // If the Wifi isn't out ask server for authorization
        // Publish our uid string to find out if its authorized
        uint16_t packetIdPub1 = mqttClient.publish("rfid/auth/req", 1, false, progParams->card.uidStr); // Publish the read UID to rfid/auth/req. BE VERY CAREFUL TO NOT SET THE RETAIN FLAG
        Serial.print("Publishing at QoS 1, packetId: ");
        Serial.println(packetIdPub1);
      }

      Serial.print("EventBits:");
      Serial.println(xEventGroupGetBits(rfidStatesGroup));

      // Now suspend our polling task until we hear back from MQTT broker
      vTaskSuspend ( NULL ); // Suspend ourselves. No need to keep polling now that there is a card
    }
      
    else {
      // No new card
      Serial.print("EventBits:");
      Serial.println(xEventGroupGetBits(rfidStatesGroup));
    }
  }
}

/* Checks for continued presence of an RFID card at the reader. Goes to Timeout if card removed or Collision if >1 card present
*/
void pollPresTask (void *params){
  //UBaseType_t uxHighWaterMark;
  EventBits_t uxBits;
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (CARD_BIT_0 | AUTH_BIT_1 ); // This creates a mask for checking our bits. To make mask we always use |
  byte resultWake;
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);

  metaStruct *progParams = (metaStruct*) params; 
  
  
  /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
  Serial.print("pollPres high watermark init:");
  Serial.println(uxHighWaterMark);
  Serial.println("##");*/

  
  Serial.print("EventBits:");
  Serial.println(xEventGroupGetBits(rfidStatesGroup));
  
  
  for(;;){
    // Wait for CARD_BIT_0 and AUTH_BIT_1 to be set before proceeding
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdFALSE, pdTRUE, xTicksToWait);

    xSemaphoreTake(SPIMutexHandle, portMAX_DELAY); // Must take the SPIMutex if you wish to proceed. This prevents presPoll and collPoll from trying to access SPI bus at same time

    /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.print("pollPres high watermark task:");
    Serial.println(uxHighWaterMark);*/
    
    vTaskDelay(MS_POLL_TIMER_PERIOD); // Only poll every 100ms?
    
    Serial.print("EventBits:");
    Serial.println(xEventGroupGetBits(rfidStatesGroup));

    pollPres(progParams, &rfidStatesGroup, &blinkLEDHandle0, &blinkLEDHandle1, &pollNewHandle);

    xSemaphoreGive(SPIMutexHandle); // Now give up the Mutex handle so collPoll may execute
  }
}

/* Task to handle timeout condition
Is blocked but TIMEOUT_BIT_3 and auto clears on xEventGroupClearBits
Suspends LED tasks and opens relays (clears RELAY_BIT_2 as well)
*/
void timeoutTask (void *params){

  metaStruct *progParams = (metaStruct*) params;

  //UBaseType_t uxHighWaterMark;
  EventBits_t uxBits;
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (TIMEOUT_BIT_3); // This creates a mask for checking our bits. To make mask we always use |
  
  /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
  Serial.print("High watermark:");
  Serial.println(uxHighWaterMark);
  Serial.println("##");*/
  
  for(;;){
   
    // Auto clear bits seems logical here however it prevents the timeout task from being interrupted (not an actual interrupt) by the arrival of a new auth card
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdFALSE, pdTRUE, xTicksToWait); 
    
    // Publish our UID to end of use topic RIGHT AWAY! If we wait for the full timeout someone could interrupt with a new card.
    uint16_t packetIdPub1 = mqttClient.publish("rfid/auth/eou", 1, false, progParams->card.uidStr); // Publish the read UID to rfid/auth/req. BE VERY CAREFUL TO NOT SET THE RETAIN FLAG
    Serial.print("Publishing at QoS 1, packetId: ");
    Serial.println(packetIdPub1);

    vTaskResume(closeRelayHandle);    // If we are in timeout we know that CARD_BIT_0 and AUTH_BIT_1 have been cleared therefore we must resume the closeRelayTask

    // Check how we got to timeout
    // Bitmask our GetBits to just the combination of bit we care about
    // TIMEOUT_BIT_3 and COLL_BIT_4
    if ((xEventGroupGetBits(rfidStatesGroup) & 24) == 24){
      Serial.println("Timeout because a collision"); // If we have come here from a collision we mustr 
    }
    else if ((xEventGroupGetBits(rfidStatesGroup) & 24) == 8){
      Serial.println("Timeout because a card was removed");
      vTaskResume(pollNewHandle); // If we have come here from timeout we allow for a new card to interrupt and prevent the relay from being opened
    }
    
    // Block task until timeout has expired
    vTaskDelay(MS_TIMEOUT_PERIOD); 
    // Two step - block again if the TIMEOUT_BIT_3 is cleared elsewhere (ie. in MQTT AUTH - a new authorized card has appeared)
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdFALSE, pdTRUE, xTicksToWait);

    //uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.println("Timeout!");
    Serial.print("EventBits:");
    Serial.println(xEventGroupGetBits(rfidStatesGroup));
    
    // Turn LEDs off
    progParams->LEDParams0.myColour = CRGB::Black; 
    progParams->LEDParams1.myColour = CRGB::Black;
    // Set both LEDs to not blink
    progParams->LEDParams0.blink = 0;
    progParams->LEDParams1.blink = 0;

    digitalWrite(RELAY_PIN, LOW); // Open relay because we've timed out
    //vTaskResume(closeRelayHandle); // We must resume the closeRelayTask that suspends after running once
    xEventGroupClearBits(rfidStatesGroup, RELAY_BIT_2|TIMEOUT_BIT_3|COLL_BIT_4); // Clear the relay, collision and timeout bit


    // Force waiting for timeout to occur if we got here because of a collision
    vTaskResume(pollNewHandle); // Now we are safe to resume

      }
}

void collPollTask (void *params){ // Must sync with pollPres

  metaStruct *progParams = (metaStruct*) params;

  EventBits_t uxBits;
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (CARD_BIT_0 | AUTH_BIT_1 ); // This creates a mask for checking our bits. To make mask we always use |
  char collCounter = 0;

  UBaseType_t uxHighWaterMark;



  /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
  Serial.print("Init collPoll high watermark:");
  Serial.println(uxHighWaterMark);
  Serial.println("##");*/
  
  for(;;){
    
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdFALSE, pdTRUE, xTicksToWait); // Block until card and auth bits are set
    
    xSemaphoreTake(SPIMutexHandle, portMAX_DELAY); // Must have the SPI mutex inorder to proceed. 

    /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.print("Active collPoll high watermark:");
    Serial.println(uxHighWaterMark);*/
    
    // Collision polling function call
    collPolling (progParams, &blinkLEDHandle0, &blinkLEDHandle1, &rfidStatesGroup, &pollNewHandle);

    xSemaphoreGive(SPIMutexHandle); // Now give up the Mutex handle so presPoll may execute
  }
}


 /////////////////////////////////////////  Emergency Stop Tasks   /////////////////////////////////////////

void eStopFireTask (void *params){
  /*UBaseType_t uxHighWaterMark;
  uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation*/
  
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (ESTOPFIRE_BIT_5); // This creates a mask for checking our bits. To make mask we always use |
  EventBits_t uxBits;    
  
  metaStruct *progParams = (metaStruct*) params;
  
  /*Serial.print("eStop fire high watermark init:");
  Serial.println(uxHighWaterMark);
  Serial.println("##");*/

  for(;;){

    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdTRUE, pdTRUE, xTicksToWait); // Block until the estop bit is set && clear the bit on return
    /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.print("Fire estop fire high watermark running");
    Serial.println(uxHighWaterMark);*/

    // Check rfidStatesGroup to determine what state we are currently in
    if (xEventGroupGetBits(rfidStatesGroup) == 0){ // If no cards are present just shut everything down
      // We only need to suspend pollNew as it is the entry point for all of the RFID functionality
      vTaskSuspend(pollNewHandle); // We only need to suspend pollNew because all other tasks are gated by EventBits
      // No need to open the relay because if EventBits = 0 it will not be open
      Serial.println("Stop work!");
    }
    else{ // If our state is anything other than no card present we'll have to clear all the event bits
      xEventGroupClearBits(rfidStatesGroup, CARD_BIT_0|AUTH_BIT_1|RELAY_BIT_2|TIMEOUT_BIT_3|COLL_BIT_4);
      digitalWrite(RELAY_PIN, LOW); // Open the relay
      vTaskSuspend(pollNewHandle); // Then we can suspend pollNew

      Serial.println("Stop work!");
    }

    // Set both LEDS to Green
    progParams->LEDParams0.myColour = CRGB::Yellow; 
    progParams->LEDParams1.myColour = CRGB::Yellow;
    // Set both LEDs to not blink
    progParams->LEDParams0.blink = 1;
    progParams->LEDParams1.blink = 1;

    vTaskResume(blinkLEDHandle0);
    vTaskResume(blinkLEDHandle1);

    //Serial.print("Clear estop task high watermark running");
    //Serial.println(uxHighWaterMark);
  }
}

void eStopClearTask (void *params){
  
  //UBaseType_t uxHighWaterMark;
  //uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
  
  const TickType_t xTicksToWait = portMAX_DELAY; // Wait forever 
  const EventBits_t xBitsToWaitFor = (ESTOPCLEAR_BIT_6); // This creates a mask for checking our bits. To make mask we always use |
  EventBits_t uxBits;  
  
  metaStruct *progParams = (metaStruct*) params;

  /*Serial.print("eStop clear high watermark init:");
  Serial.println(uxHighWaterMark);
  Serial.println("##");*/
  
  
  for(;;){
    uxBits = xEventGroupWaitBits(rfidStatesGroup, xBitsToWaitFor, pdTRUE, pdTRUE, xTicksToWait); // Block until the estop bit is set
    
    /*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL ); // Returns the minimum amount of unused stack space available since task creation
    Serial.print("Clear estop task high watermark running");
    Serial.println(uxHighWaterMark);
    Serial.println("Back to work!");*/
    
    // Set both LEDS to Green
    progParams->LEDParams0.myColour = CRGB::Black; 
    progParams->LEDParams1.myColour = CRGB::Black;
    // Set both LEDs to not blink
    progParams->LEDParams0.blink = 0;
    progParams->LEDParams1.blink = 0;

    vTaskResume(blinkLEDHandle0);
    vTaskResume(blinkLEDHandle1);
    
    vTaskResume(pollNewHandle);
  }
}

/////////////////////////////Software Timer callbacks //////////////////////////////////////////

/* Callback function triggered by setting the LEDparams.blinkTemp flag.
Period of timer is constrained by #define LEDBLINK_PERIOD. When callback executes the LEDs are toggled out of the blink state and set to black.
*/
void LEDTimerCallback (TimerHandle_t LEDblinkTimer){
  metaStruct *progParams = (metaStruct*) pvTimerGetTimerID (LEDblinkTimer);


  Serial.println("Blink timer!");
  progParams->LEDParams0.myColour = CRGB::Black;
  progParams->LEDParams1.myColour = CRGB::Black;
  progParams->LEDParams0.blink = 0;
  progParams->LEDParams1.blink = 0;
}


void setup() {
  
  // Variables
  metaStruct progParams; // Create an instance of our metaStuct that holds our timers and LED parameters
  
  // Initialize our LED parameters
  progParams.LEDParams0.myColour = CRGB::Black;
  progParams.LEDParams1.myColour = CRGB::Black;
  progParams.LEDParams0.led = 0;
  progParams.LEDParams1.led = 1;
  progParams.LEDParams0.blink = 0;
  progParams.LEDParams1.blink = 0;
  progParams.LEDParams0.blinkPeriod = 200;
  progParams.LEDParams1.blinkPeriod = 200;
  // Initialization of our collision counter
  progParams.card.collCounter = 0;

  
  Serial.begin(115200);

  toolAccessInit(); // Call initialization function as per usual no need for RTOS tasking

  // Event group creation
  rfidStatesGroup = xEventGroupCreate();
  // Check rdfidStatesGroup was created successfully
  if( rfidStatesGroup == NULL ){
    Serial.println("Event group creation failure due to insufficient heap");
  }
  else{
    Serial.println("Eventgroup created successfully");
  }

  // Mutex/Semaphore creation
  SPIMutexHandle = xSemaphoreCreateMutex();
  if ( SPIMutexHandle == NULL){
    Serial.println("Mutex could not be created.");
  }


  // Task creation 

  //xTaskCreatePinnedToCore(basicTask, "basicTask", 1024, NULL, 1, &basicTaskHandle, 1);
  xTaskCreatePinnedToCore(blinkyLED, "blinkLED", 1024, &progParams.LEDParams0, 1, &blinkLEDHandle0, 1);
  xTaskCreatePinnedToCore(blinkyLED, "blinkLED1", 1024, &progParams.LEDParams1, 1, &blinkLEDHandle1, 1);
  
  // Polling task that waits on !CARD_BIT_0 
  xTaskCreatePinnedToCore(pollNewTask, "pollNewTask", 1024, &progParams, 1, &pollNewHandle, 1);
  xTaskCreatePinnedToCore(closeRelayTask, "closeRelay", CONFIG_SYSTEM_EVENT_TASK_STACK_SIZE, &progParams, 1, &closeRelayHandle, 1); 
  xTaskCreatePinnedToCore(pollPresTask, "pollPresTask", 1024, &progParams, 1, &pollPresHandle, 1);
  xTaskCreatePinnedToCore(timeoutTask, "TimeoutTask", 1024, &progParams, 1, &timeoutHandle, 1);
  xTaskCreatePinnedToCore(collPollTask, "TimeoutTask", 1024, &progParams, 1, &collPollHandle, 1);
  xTaskCreatePinnedToCore(eStopFireTask,"eStopFireTask", 1024, &progParams, 1, &eStopFireHandle, 1 );
  xTaskCreatePinnedToCore(eStopClearTask,"eStopClearTask", 1024, &progParams, 1, &eStopClearHandle, 1 );

  // CONFIG_SYSTEM_EVENT_TASK_STACK_SIZE


  /* xTimerCreate creates a dormant software timer (ie. it is not yet running)
    Software timers MUST NOT call RTOS API that are task blocking
    All software timers share a command que controlled by RTOS daemon
  
    The task connectToMqtt is provided as the for our software timer TimerCallBackFunction (notice that we must cast it as such)
   pg. 178 Mastering FreeRTOS: https://www.freertos.org/wp-content/uploads/2018/07/161204_Mastering_the_FreeRTOS_Real_Time_Kernel-A_Hands-On_Tutorial_Guide.pdf
  */  
  mqttReconnectTimer = xTimerCreate("mqttTimer", MS_MQTT_RECONNECT_PERIOD, pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  //(const char * const pcTimerName, TickType_t xTimerPeriodInTicks, UBaseType_t uxAutoRelaod, void * pvTimerID, TimerCallBackFunction_t pxCallbackFunction
  wifiReconnectTimer = xTimerCreate("wifiTimer", MS_WIFI_RECONNECT_PERIOD, pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  // Both timers are moved from Dormant to Running in the WiFiEvent task because we will only need to attempt the callbackTask in the event of a WiFi outage
  LEDblinkTimer = xTimerCreate("LEDblinkTimer", LEDBLINK_PERIOD, pdFALSE, (void*)&progParams, reinterpret_cast<TimerCallbackFunction_t>(LEDTimerCallback));
  // Create a one shot timer for when we want LEDs to only blink for a set period constrained by LEDBLINK_PERIOD
 
  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();

while(1){
  vTaskDelay(1000/portTICK_PERIOD_MS);
}

}

void loop() {

}
 
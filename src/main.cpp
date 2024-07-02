/*// - - - - -
// ESPDMX - A Arduino library for sending and receiving DMX using the builtin serial hardware port.
//
// Copyright (C) 2015  Rick <ricardogg95@gmail.com>
// This work is licensed under a GNU style license.
//
// Last change: Musti <https://github.com/IRNAS> (edited by Musti)
//
// Documentation and samples are available at https://github.com/Rickgg/ESP-Dmx
// Connect GPIO02 - TDX1 to MAX3485 or other driver chip to interface devices
// Pin is defined in library
// - - - - -

#include <ESPDMX.h>

DMXESPSerial dmx;

void setup() {
  dmx.init();               // initialization for first 32 addresses by default
  //dmx.init(512)           // initialization for complete bus
  delay(200);               // wait a while (not necessary)
}

void loop() {

    dmx.write(3, 0);        // channal 3 off
    dmx.write(1, 255);      // channal 1 on
    dmx.update();           // update the DMX bus
    delay(1000);            // wait for 1s

    dmx.write(1, 0);
    dmx.write(2, 255);
    dmx.update();
    delay(1000);

    dmx.write(2, 0);
    dmx.write(3, 255);
    dmx.update();
    delay(1000);

}
*/
/*
D1 bouton
D2 datapin LED strip
D5 GND bouton

Récepteur DMX sans fil basé sur les récepteurs pour les stripleds
*/ 

#include <Arduino.h>
#include <ESPDMX.h>

DMXESPSerial dmx;


#include <EEPROM.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
WiFiManager wifiManager;
#define APNAME "mrESPNOW2DMX"
#define VERSION 15

#include <ArtnetWifi.h>
WiFiUDP UdpSend;
ArtnetWifi artnet;

#define EEPROM_SIZE 32
#define DMXMODE true
#define ARTNETMODE false
bool runningMode = DMXMODE;

#include <FastLED.h>

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN D2


// Define the array of leds
#define MAXLEDLENGTH 144
//#define MAXLEDLENGTH 10 // en mode programmation quand le ledstrip est alimenté via l'ESP, on se limite à 10 leds (pour ne pas le brûler)
CRGB leds[MAXLEDLENGTH];
uint8_t ledsTemp[MAXLEDLENGTH][3];


// #include <TM1637Display.h>
// TM1637Display display(D7, D6); // clck DIO

#include "OneButton.h"
// Setup a new OneButton on pin D6.  


#include <ESP8266WiFiMulti.h>
#include <espnow.h>
// 1 DMX RECEIVER : 3C:61:05:D1:CC:57 
// 2 mrLEDTUBE : 3C:61:05:D3:19:32 

int setupAddress = 1;
int setupMode = 1;
int setupTubeNumber = 1;

double lastOffset = 0;

double lastOffsetR = 0;
double lastOffsetG = 0;
double lastOffsetB = 0;

double offsetInc = 0.1;

double offsetIncR = 0.1;
double offsetIncG = 0.1;
double offsetIncB = 0.1;

#define RUNNING true
#define SETUP false

bool etat = RUNNING;

int flashInterval;

uint8_t dmxChannels[512];

typedef struct struct_message {
    uint8_t status;
    uint8_t data;    
} struct_message;


struct_message incomingMessage;
struct_message outgoingMessage;

typedef struct struct_dmx_message {
    uint8_t dmx001;
    uint8_t dmx002;
    uint8_t dmx003;
    uint8_t dmx004;
        
} struct_dmx_message;

struct_dmx_message incomingDMXMessage;

typedef struct struct_dmx_packet {
    uint8_t blockNumber; // on divise les 512 adresses en 4 blocs de 128 adresses
    uint8_t dmxvalues[128];   
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;


void OnDataSent(u8 *mac_addr, u8 status) {  
     
}


// Callback when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));  
  uint8_t packetNumber = incomingDMXPacket.blockNumber;
  //Serial.print("ondatarecv - dmxvalues1 = ");
  //Serial.println(incomingDMXPacket.dmxvalues[1]);
  for(int i=0;i<128;i++)
  {
    dmxChannels[(packetNumber*128)+i]=incomingDMXPacket.dmxvalues[i];
    //dmx.write((packetNumber*128)+i+1,incomingDMXPacket.dmxvalues[i]);
    
  }
  // dmx.update();           // update the DMX bus
  // delay(100);
 
}






double mrdoublemodulo(double nombre, double diviseur)
{
  while(nombre<0)nombre+=diviseur;
  while(nombre>=diviseur)nombre-=diviseur;
  return nombre;
}


void setup() {
  //Serial.begin(115200);
  //Serial.println("début setup");
dmx.init(512);

  //dmx.init(512);

  

  
  // affichage de la version (blink)
  //ledProgress(VERSION,100);


  if(true)// démarrage en mode DMX (on n'appuye pas sur le bouton au démarrage)
  {
    runningMode = DMXMODE;
    
    

    //pinMode(LED_BUILTIN,OUTPUT);
  
  WiFi.disconnect();
  ESP.eraseConfig();
 
  // Wifi STA Mode
  WiFi.mode(WIFI_STA);
  // Get Mac Add
  
  
  // Initializing the ESP-NOW
   if (esp_now_init() != 0) {
    
    return;
     }

    esp_now_register_recv_cb(OnDataRecv);

  }
 
    

  //  delay(5000);
    
    
  
  
  

  
  
  
  




  


  

}

void loop() {
  
  //   dmx.write(130, 200);        // channal 3 off
 
  //   dmx.update();           // update the DMX bus
  //   delay(1000);            // wait for 1s

  //  dmx.write(130, 0);        // channal 3 off
 
  //   dmx.update();           // update the DMX bus
  //   delay(1000);            // wait for 1s

 for(int i=0;i<511;i++)
 {  
   dmx.write(i+1,dmxChannels[i]);
 }
 dmx.update();
 //delay(1000);

//     Serial.print("dmxChannels[1]= ");
//   Serial.println(dmxChannels[1]);
//   delay(10);
  

  
    




}


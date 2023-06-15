/* version fonctionnelle du 15 06 2023
D1 bouton
D2 datapin LED strip
D5 GND bouton
75 LEDs
Mode 0 : 123 RGB for all strip at once
Mode 1 : 123 RGB + 4 LENGTH + 5 OFFSET    
Mode 2 : 123 RGB + 4 LENGTH + 5 OFFSET tapered
Mode 3 : 123 RGB + 4 LENGTH + 5 OFFSET + 6 OFFSET TUBE  
Mode 4 : 123 RGB + 4 LENGTH + 5 OFFSET + 6 OFFSET TUBE (tapered)
Mode 5 : individual rgb 123 456 ...

SETUP (clic long pour y accéder ou en sortir) : réglage du numéro de groupe
Le nombre de LEDS correspondant au numéro de groupe clignote
Le numéro de groupe est enregistré en EEPROM
*/ 

#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_SIZE 32

#include <FastLED.h>

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN D2


// Define the array of leds
#define MAXLEDLENGTH 75
CRGB leds[MAXLEDLENGTH];


// #include <TM1637Display.h>
// TM1637Display display(D7, D6); // clck DIO

#include "OneButton.h"
OneButton button1(D1, true);
// Setup a new OneButton on pin D6.  


#include <ESP8266WiFiMulti.h>
#include <espnow.h>
// 1 DMX RECEIVER : 3C:61:05:D1:CC:57 / COM17 serial …BIA
// 2 mrLEDTUBE : 3C:61:05:D3:19:32  COM18 serial 7

int setupAddress = 1;
int setupMode = 1;
int setupTubeNumber = 1;

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
  for(int i=0;i<128;i++)
  {
    dmxChannels[(packetNumber*128)+i]=incomingDMXPacket.dmxvalues[i];
  }
 
}

void DMX2LEDSTRIP()
{

FastLED.clear();

setupMode = dmxChannels[0];
int ledstart; 
int ledmiddle;
int ledend;
int ledlength;
double ledDimmer;
double ledDimmerIncrement;

int ledoffset = 0;
switch (setupMode)
{
case 0 : // 123 RGB for all strip at once
  for(int j=0;j<  MAXLEDLENGTH;j++)
  {
      leds[j].r=dmxChannels[1];  
      leds[j].g=dmxChannels[2];  
      leds[j].b=dmxChannels[3];        
  }
  break;

  case 1 : // 123 RGB + 4 LENGTH + 5 OFFSET    
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5];
    ledstart = ledoffset-ledlength;    
    ledend = ledstart+ledlength;
    if(ledstart<0)ledstart=0;        
    if(ledend>MAXLEDLENGTH)ledend=MAXLEDLENGTH;
    
      for(int j=ledstart;j<ledend;j++)
      {
      leds[j].r=dmxChannels[1];  
      leds[j].g=dmxChannels[2];  
      leds[j].b=dmxChannels[3];        
      }
  break;

    case 2 : // 123 RGB + 4 LENGTH + 5 OFFSET tapered
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5];
    ledstart = ledoffset-ledlength;    
    ledend = ledstart+ledlength;
    
    ledlength = ledend-ledstart;
    ledmiddle = ledstart + (ledlength/2);    
    ledDimmerIncrement = 1.0/(double)(ledlength/2);

      ledDimmer = 0;
      for(int j=ledstart;j<ledmiddle;j++)
      {
        ledDimmer+=ledDimmerIncrement;
        if(j<0 || j>MAXLEDLENGTH)continue;
      leds[j].r=dmxChannels[1]*ledDimmer;  
      leds[j].g=dmxChannels[2]*ledDimmer;  
      leds[j].b=dmxChannels[3]*ledDimmer;        
      
      }

      ledDimmer = 1+ledDimmerIncrement;
      for(int j=ledmiddle;j<ledend;j++)
      {
        ledDimmer-=ledDimmerIncrement;
        if(j<0 || j>MAXLEDLENGTH)continue;
      leds[j].r=dmxChannels[1]*ledDimmer;  
      leds[j].g=dmxChannels[2]*ledDimmer;  
      leds[j].b=dmxChannels[3]*ledDimmer;        
      
      }
  break;

  case 3 : // 123 RGB + 4 LENGTH + 5 OFFSET + 6 OFFSET TUBE  
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5]+(dmxChannels[6]*setupTubeNumber);
    ledstart = ledoffset-ledlength;    
    ledend = ledstart+ledlength;
    if(ledstart<0)ledstart=0;        
    if(ledend>MAXLEDLENGTH)ledend=MAXLEDLENGTH;
    
      for(int j=ledstart;j<ledend;j++)
      {
      leds[j].r=dmxChannels[1];  
      leds[j].g=dmxChannels[2];  
      leds[j].b=dmxChannels[3];        
      }
  break;

  case 4 : // 123 RGB + 4 LENGTH + 5 OFFSET + 6 OFFSET TUBE (tapered)
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5]+(dmxChannels[6]*setupTubeNumber);
    ledstart = ledoffset-ledlength;    
    ledend = ledstart+ledlength;
    
    ledlength = ledend-ledstart;
    ledmiddle = ledstart + (ledlength/2);    
    ledDimmerIncrement = 1.0/(double)(ledlength/2);

      ledDimmer = 0;
      for(int j=ledstart;j<ledmiddle;j++)
      {
        ledDimmer+=ledDimmerIncrement;
        if(j<0 || j>MAXLEDLENGTH)continue;
      leds[j].r=dmxChannels[1]*ledDimmer;  
      leds[j].g=dmxChannels[2]*ledDimmer;  
      leds[j].b=dmxChannels[3]*ledDimmer;        
      
      }

      ledDimmer = 1+ledDimmerIncrement;
      for(int j=ledmiddle;j<ledend;j++)
      {
        ledDimmer-=ledDimmerIncrement;
        if(j<0 || j>MAXLEDLENGTH)continue;
      leds[j].r=dmxChannels[1]*ledDimmer;  
      leds[j].g=dmxChannels[2]*ledDimmer;  
      leds[j].b=dmxChannels[3]*ledDimmer;        
      
      }
  break;

  

  case 5 : // individual rgb 123 456 ...
  for(int j=0;j<MAXLEDLENGTH*3;j+=3)
  {
      leds[j/3].r=dmxChannels[ledoffset+j+1];  
      leds[j/3].g=dmxChannels[ledoffset+j+2];  
      leds[j/3].b=dmxChannels[ledoffset+j+3];        
  }
  break;

default:
  break;
}

    
    FastLED.show();
    delay(1);



}

// ----- button 1 callback functions

void click1() {//incrémente le numéro de groupe  
   if(etat==RUNNING)return; // en mode RUNNING, on ignore cette action
   setupTubeNumber=(setupTubeNumber+1)%10;

} 

void longPressStart1() {
  Serial.print("longpress | etat = ");
  Serial.println((etat?"RUNNING":"SETUP"));

  if(etat==SETUP) // avant de sortir du SETUP, on enregistre les données en mémoire persistante
  {    
    EEPROM.write(0,setupAddress);
    EEPROM.write(4,setupMode);
    EEPROM.write(8,setupTubeNumber);
    Serial.print("Commit =  ");
    Serial.println(EEPROM.commit());
    
  }
  
  etat=!etat; // on passe de RUNNING à SETUP ou inversement





}

void setup() {
  // on utilise D5 comme GND pour le bouton 1
  pinMode(D5,OUTPUT);
  digitalWrite(D5,LOW);

    // link the button 1 functions.    
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  
  pinMode(LED_BUILTIN,OUTPUT);
  Serial.begin(115200);
  WiFi.disconnect();
  ESP.eraseConfig();
 
  // Wifi STA Mode
  WiFi.mode(WIFI_STA);
  // Get Mac Add
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("\nESP-Now Receiver");
  
  // Initializing the ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Problem during ESP-NOW init");
    return;
  }
  
EEPROM.begin(EEPROM_SIZE);
uint eeAddress=0;

setupAddress = EEPROM.read(0);
setupMode = EEPROM.read(4);
setupTubeNumber = EEPROM.read(8);
if((setupAddress<1)||(setupAddress>512))setupAddress=1;
if((setupMode<1)||(setupMode>255))setupMode=1;
if((setupTubeNumber<0)||(setupTubeNumber>32))setupTubeNumber=0;

//EEPROM.end();

Serial.print(" ");
Serial.print("Paramètres récupérés en EEPROM : Addresse = ");
Serial.print(setupAddress);
Serial.print(" | Mode = ");
Serial.print(setupMode);
Serial.print(" | Tube Group = ");
Serial.println(setupTubeNumber);
  //esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  // We can register the receiver callback function
  esp_now_register_recv_cb(OnDataRecv);
// esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
// REPLACE WITH RECEIVER MAC Address
// uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// uint8_t mrDMXRECEIVERAddress[] = {0x3C, 0x61, 0x05, 0xD1, 0xCC, 0x57};
  // Register peer
  //esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH);  // GRB ordering is typical  
}

void loop() {
  button1.tick();

  if(etat==RUNNING)
  {
    DMX2LEDSTRIP();
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube
  {
    FastLED.clear();
    for(int j=0;j<setupTubeNumber;j++)
    {
      leds[j].r=0;
      leds[j].g=150;
      leds[j].b=0;
    }
    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }



}


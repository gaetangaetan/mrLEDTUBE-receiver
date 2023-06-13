// test git 3
#include <Arduino.h>
#include <FastLED.h>

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN D2
#define CLOCK_PIN D1

// Define the array of leds
#define MAXLEDLENGTH 10
CRGB leds[MAXLEDLENGTH];


// #include <TM1637Display.h>
// TM1637Display display(D7, D6); // clck DIO

#include "OneButton.h"
OneButton button1(D6, true);
// Setup a new OneButton on pin D6.  


#include <ESP8266WiFiMulti.h>
#include <espnow.h>
// 1 DMX RECEIVER : 3C:61:05:D1:CC:57 / COM17 serial …BIA
// 2 mrLEDTUBE : 3C:61:05:D3:19:32  COM18 serial 7

int setupAddress = 1;
int setupMode = 1;
int setupLength = 10;
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
  //  Serial.print(incomingDMXMessage.dmx001);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx002);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx003);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx004);
  //  Serial.println(" ");
   //delay(50);

  // digitalWrite(LED_BUILTIN,LOW);
  // delay(100);
  // digitalWrite(LED_BUILTIN,HIGH);
  // Serial.println("data received");
  //delay(incomingMessage.data); 
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
  for(int j=0;j<  setupLength;j++)
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
  for(int j=0;j<setupLength*3;j+=3)
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

// This function will be called when the button1 was pressed 1 time (and no 2. button press followed).
void click1() {
  Serial.print("click - setuptubenumber = ");
  Serial.println(setupTubeNumber);
   if(etat==RUNNING)return; // en mode RUNNING, on ignore cette action
   setupTubeNumber=(setupTubeNumber+1)%10;

} // click1

void longPressStart1() {
  Serial.print("longpress - etat = ");
  Serial.println(etat);
  etat=!etat; // on passe de RUNNING à SETUP ou inversement

}

void setup() {

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
  
  //esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  // We can register the receiver callback function
  esp_now_register_recv_cb(OnDataRecv);
// esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
// REPLACE WITH RECEIVER MAC Address
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t mrDMXRECEIVERAddress[] = {0x3C, 0x61, 0x05, 0xD1, 0xCC, 0x57};
  // Register peer
  //esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH);  // GRB ordering is typical  
}

void loop() {
  if(etat==RUNNING)
  {
    DMX2LEDSTRIP();
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube
  {
    FastLED.clear();
    for(int j=0;j<setupTubeNumber;j++)
    {
      leds[j].r=255;
      leds[j].g=255;
      leds[j].b=255;
    }
    FastLED.show();
    delay(100);
    FastLED.clear();
    delay(100);
  }



}


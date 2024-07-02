// version artnet en cours
// deuxième commit
// troisième commit

/* basée sur version fonctionnelle du 15 06 2023
D1 bouton
D2 datapin LED strip
D5 GND bouton
75 LEDs
Mode 0 : 234 RGB for all strip at once
Mode 1 : 234 RGB + 5 LENGTH + 6 OFFSET    
Mode 2 : 234 RGB + 5 LENGTH + 6 OFFSET tapered
Mode 3 : 234 RGB + 5 LENGTH + 6 OFFSET + 7 OFFSET TUBE  
Mode 4 : 234 RGB + 5 LENGTH + 6 OFFSET + 7 OFFSET TUBE (tapered)
Mode 5 : individual rgb 234 567 ...
Mode 6 : RGB séparé pour chaque groupe : 234 RGB GRP0, 567 RGB GRP1, 8910 RGB GRP2, ...
Mode 7 : FX1 Segments montant : 6 canaux par groupe : 234 RGB + 5 longueur segment + 6 longueur espace (pixels éteints entre deux segments) + 7 speed (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
Mode 8 : FX2 Segments montant R G B : 12 canaux par groupe : 234 RGB + 5 6 7 longueur segment rgb + 8 9 10 longueur espace rgb (pixels éteints entre deux segments) + 11 12 13 speed r g b (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
Mode 255 : on affiche le numéro du groupe (0 à 10)

SETUP (clic long pour y accéder ou en sortir) : réglage du numéro de groupe
Le nombre de LEDS correspondant au numéro de groupe clignote
Le numéro de groupe est enregistré en EEPROM
*/ 

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
WiFiManager wifiManager;
#define APNAME "mrLEDTUBE15"
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
OneButton button1(D1, true);
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
  for(int i=0;i<128;i++)
  {
    dmxChannels[(packetNumber*128)+i]=incomingDMXPacket.dmxvalues[i];
  }
 
}

void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
  
}

void update_finished() {
  
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total) {
  int progress = (MAXLEDLENGTH*cur)/total;
  FastLED.clear();
    for(int j=0;j<progress;j++)
    {
      leds[j].r=0;
      leds[j].g=0;
      leds[j].b=150;
    }
    FastLED.show();
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {
  FastLED.clear();
    for(int j=0;j<MAXLEDLENGTH;j++)
    {
      leds[j].r=150;
      leds[j].g=0;
      leds[j].b=0;
    }
    
    FastLED.show();
  
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

void updateFirmware()
{

  ESPhttpUpdate.setClientTimeout(2000);  // default was 8000
  
  if ((WiFi.status() == WL_CONNECTED)) {

    WiFiClient client;

    // The line below is optional. It can be used to blink the LED on the board during flashing
    // The LED will be on during download of one buffer of data from the network. The LED will
    // be off during writing that buffer to flash
    // On a good connection the LED should flash regularly. On a bad connection the LED will be
    // on much longer than it will be off. Other pins than LED_BUILTIN may be used. The second
    // value is used to put the LED on. If the LED is on with HIGH, that value should be passed
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);

    // Add optional callback notifiers
    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, "http://mrledtubefirmware.gaetanstreel.com/firmware.bin");
    // Or:
    // t_httpUpdate_return ret = ESPhttpUpdate.update(client, "server", 80, "file.bin");

    switch (ret) {
      case HTTP_UPDATE_FAILED: Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str()); 
                FastLED.clear();
        for(int j=0;j<MAXLEDLENGTH;j++)
        {
          leds[j].r=150;
          leds[j].g=0;
          leds[j].b=0;
        }    
        FastLED.show();
        delay(2000);
        break;

      case HTTP_UPDATE_NO_UPDATES: Serial.println("HTTP_UPDATE_NO_UPDATES"); break;

      case HTTP_UPDATE_OK: Serial.println("HTTP_UPDATE_OK"); 
          FastLED.clear();
        for(int j=0;j<MAXLEDLENGTH;j++)
        {
          leds[j].r=0;
          leds[j].g=150;
          leds[j].b=0;
        }    
        FastLED.show();
        delay(2000);
      break;
    }
  }
  
}

double mrdoublemodulo(double nombre, double diviseur)
{
  while(nombre<0)nombre+=diviseur;
  while(nombre>=diviseur)nombre-=diviseur;
  return nombre;
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
  int onLength;
  int offLength;
  int intoffset;
  int fxSpeed;

  int onLengthR;
  int offLengthR;
  int intoffsetR;
  int fxSpeedR;

  int onLengthG;
  int offLengthG;
  int intoffsetG;
  int fxSpeedG;

  int onLengthB;
  int offLengthB;
  int intoffsetB;
  int fxSpeedB;

  int ir = 3 * setupTubeNumber + 1;
  int ig = 3 * setupTubeNumber + 2;
  int ib = 3 * setupTubeNumber + 3;

  int ledoffset = 0;
  int pos = 0;

  int groupChannelOffset;
  /*
  1 mode général (9 = on utilise "mode groupe")
  2 r
  3 g
  4 b
  5 param 1
  6 param 2
  7 param 3
  8 param 4
  9 param 5
  10 param 6
  11 param 7
  12 param 8
  13 param 9
  14 mode groupe
  */
  switch (setupMode)
  {
  case 0: // 234 RGB for all strip at once

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  case 1: // 234 RGB + 5 LENGTH + 6 OFFSET
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5];
    ledstart = ledoffset - ledlength;
    ledend = ledstart + ledlength;
    if (ledstart < 0)
      ledstart = 0;
    if (ledend > MAXLEDLENGTH)
      ledend = MAXLEDLENGTH;

    for (int j = ledstart; j < ledend; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  case 2: // 234 RGB + 5 LENGTH + 6 OFFSET tapered
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5];
    ledstart = ledoffset - ledlength;
    ledend = ledstart + ledlength;

    ledlength = ledend - ledstart;
    ledmiddle = ledstart + (ledlength / 2);
    ledDimmerIncrement = 1.0 / (double)(ledlength / 2);

    ledDimmer = 0;
    for (int j = ledstart; j < ledmiddle; j++)
    {
      ledDimmer += ledDimmerIncrement;
      if (j < 0 || j > MAXLEDLENGTH)
          continue;
      leds[j].r = dmxChannels[1] * ledDimmer;
      leds[j].g = dmxChannels[2] * ledDimmer;
      leds[j].b = dmxChannels[3] * ledDimmer;
    }

    ledDimmer = 1 + ledDimmerIncrement;
    for (int j = ledmiddle; j < ledend; j++)
    {
      ledDimmer -= ledDimmerIncrement;
      if (j < 0 || j > MAXLEDLENGTH)
          continue;
      leds[j].r = dmxChannels[1] * ledDimmer;
      leds[j].g = dmxChannels[2] * ledDimmer;
      leds[j].b = dmxChannels[3] * ledDimmer;
    }
    break;

  case 3: // 234 RGB + 5 LENGTH + 6 OFFSET + 7 OFFSET TUBE
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5] + (dmxChannels[6] * setupTubeNumber);
    ledstart = ledoffset - ledlength;
    ledend = ledstart + ledlength;
    if (ledstart < 0)
      ledstart = 0;
    if (ledend > MAXLEDLENGTH)
      ledend = MAXLEDLENGTH;

    for (int j = ledstart; j < ledend; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  case 4: // 234 RGB + 5 LENGTH + 66 OFFSET + 7 OFFSET TUBE (tapered)
    ledlength = dmxChannels[4];
    ledoffset = dmxChannels[5] + (dmxChannels[6] * setupTubeNumber);
    ledstart = ledoffset - ledlength;
    ledend = ledstart + ledlength;

    ledlength = ledend - ledstart;
    ledmiddle = ledstart + (ledlength / 2);
    ledDimmerIncrement = 1.0 / (double)(ledlength / 2);

    ledDimmer = 0;
    for (int j = ledstart; j < ledmiddle; j++)
    {
      ledDimmer += ledDimmerIncrement;
      if (j < 0 || j > MAXLEDLENGTH)
          continue;
      leds[j].r = dmxChannels[1] * ledDimmer;
      leds[j].g = dmxChannels[2] * ledDimmer;
      leds[j].b = dmxChannels[3] * ledDimmer;
    }

    ledDimmer = 1 + ledDimmerIncrement;
    for (int j = ledmiddle; j < ledend; j++)
    {
      ledDimmer -= ledDimmerIncrement;
      if (j < 0 || j > MAXLEDLENGTH)
          continue;
      leds[j].r = dmxChannels[1] * ledDimmer;
      leds[j].g = dmxChannels[2] * ledDimmer;
      leds[j].b = dmxChannels[3] * ledDimmer;
    }
    break;

  case 5: // individual rgb 234 567 ...
    for (int j = 0; j < MAXLEDLENGTH * 3; j += 3)
    {
      leds[j / 3].r = dmxChannels[ledoffset + j + 1];
      leds[j / 3].g = dmxChannels[ledoffset + j + 2];
      leds[j / 3].b = dmxChannels[ledoffset + j + 3];
    }
    break;

  case 6: // individual rgb for each tubegroup 234 567 ...
    for (int j = 0; j < MAXLEDLENGTH * 3; j += 3)
    {
      leds[j / 3].r = dmxChannels[ir];
      leds[j / 3].g = dmxChannels[ig];
      leds[j / 3].b = dmxChannels[ib];
    }
    break;

  case 7: // Mode 7 : FX1 Segments montant : 6 canaux par groupe : 234 RGB + 5 longueur segment + 6 longueur espace (pixels éteints entre deux segments) + 7 speed (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
    ir = dmxChannels[6 * setupTubeNumber + 1];
    ig = dmxChannels[6 * setupTubeNumber + 2];
    ib = dmxChannels[6 * setupTubeNumber + 3];
    onLength = dmxChannels[6 * setupTubeNumber + 4];
    offLength = dmxChannels[6 * setupTubeNumber + 5];
    fxSpeed = dmxChannels[6 * setupTubeNumber + 6] - 128;
    // onLength = 10;
    // offLength = 5;
    // ir=205;
    // ig=90;
    // ib=0;
    for (int j = 0; j < MAXLEDLENGTH; j++)
    {

      if (onLength-- > 0)
      {
          ledsTemp[j][0] = ir;
          ledsTemp[j][1] = ig;
          ledsTemp[j][2] = ib;
      }
      else if (offLength-- > 0)
      {
          ledsTemp[j][0] = 0;
          ledsTemp[j][1] = 0;
          ledsTemp[j][2] = 0;
      }
      else
      {
          onLength = dmxChannels[6 * setupTubeNumber + 4];
          offLength = dmxChannels[6 * setupTubeNumber + 5];
      }
    }

    intoffset = (int)mrdoublemodulo(lastOffset, (double)MAXLEDLENGTH);

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[(j + intoffset) % MAXLEDLENGTH].r = ledsTemp[j][0];
      leds[(j + intoffset) % MAXLEDLENGTH].g = ledsTemp[j][1];
      leds[(j + intoffset) % MAXLEDLENGTH].b = ledsTemp[j][2];
    }
    lastOffset = lastOffset + (fxSpeed * offsetInc);

    break;

  case 8: // Mode 8 : FX2 Segments montant R G B : 13 canaux par groupe : 234 RGB + 5 6 7 longueur segment rgb + 8 9 10 longueur espace rgb (pixels éteints entre deux segments) + 11 12 13 speed r g b (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
    ir = dmxChannels[6 * setupTubeNumber + 1];
    ig = dmxChannels[6 * setupTubeNumber + 2];
    ib = dmxChannels[6 * setupTubeNumber + 3];
    onLengthR = dmxChannels[6 * setupTubeNumber + 4];
    onLengthG = dmxChannels[6 * setupTubeNumber + 5];
    onLengthB = dmxChannels[6 * setupTubeNumber + 6];
    offLengthR = dmxChannels[6 * setupTubeNumber + 7];
    offLengthG = dmxChannels[6 * setupTubeNumber + 8];
    offLengthB = dmxChannels[6 * setupTubeNumber + 9];
    fxSpeedR = dmxChannels[6 * setupTubeNumber + 10] - 128;
    fxSpeedG = dmxChannels[6 * setupTubeNumber + 11] - 128;
    fxSpeedB = dmxChannels[6 * setupTubeNumber + 12] - 128;

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {

      if (onLengthR-- > 0)
      {
          ledsTemp[j][0] = ir;
      }
      else if (offLengthR-- > 0)
      {
          ledsTemp[j][0] = 0;
      }
      else
      {
          onLengthR = dmxChannels[6 * setupTubeNumber + 4];
          offLengthR = dmxChannels[6 * setupTubeNumber + 7];
      }
    }

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {

      if (onLengthG-- > 0)
      {
          ledsTemp[j][1] = ig;
      }
      else if (offLengthG-- > 0)
      {
          ledsTemp[j][1] = 0;
      }
      else
      {
          onLengthG = dmxChannels[6 * setupTubeNumber + 5];
          offLengthG = dmxChannels[6 * setupTubeNumber + 8];
      }
    }

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {

      if (onLengthB-- > 0)
      {
          ledsTemp[j][2] = ib;
      }
      else if (offLengthB-- > 0)
      {
          ledsTemp[j][2] = 0;
      }
      else
      {
          onLengthB = dmxChannels[6 * setupTubeNumber + 6];
          offLengthB = dmxChannels[6 * setupTubeNumber + 9];
      }
    }

    intoffset = (int)mrdoublemodulo(lastOffsetR, (double)MAXLEDLENGTH);
    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[(j + intoffset) % MAXLEDLENGTH].r = ledsTemp[j][0];
    }
    lastOffsetR = lastOffsetR + (fxSpeedR * offsetIncR);

    intoffset = (int)mrdoublemodulo(lastOffsetG, (double)MAXLEDLENGTH);
    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[(j + intoffset) % MAXLEDLENGTH].g = ledsTemp[j][1];
    }
    lastOffsetG = lastOffsetG + (fxSpeedG * offsetIncG);

    intoffset = (int)mrdoublemodulo(lastOffsetB, (double)MAXLEDLENGTH);
    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[(j + intoffset) % MAXLEDLENGTH].b = ledsTemp[j][2];
    }
    lastOffsetB = lastOffsetB + (fxSpeedB * offsetIncB);

    break;

  case 9: // chaque groupe utilise son propre mode (channel 14)
    groupChannelOffset = 13 * setupTubeNumber;

    switch (dmxChannels[groupChannelOffset + 13])
    {
    case 0: // 234 RGB 

      for (int j = 0; j < MAXLEDLENGTH; j++)
      {
          leds[j].r = dmxChannels[groupChannelOffset + 1];
          leds[j].g = dmxChannels[groupChannelOffset + 2];
          leds[j].b = dmxChannels[groupChannelOffset + 3];
      }
      break;

    case 1: // 234 RGB + 5 LENGTH + 6 OFFSET
      ledlength = dmxChannels[groupChannelOffset+4];
      ledoffset = dmxChannels[groupChannelOffset+5];
      ledstart = ledoffset - ledlength;
      ledend = ledstart + ledlength;
      if (ledstart < 0)
          ledstart = 0;
      if (ledend > MAXLEDLENGTH)
          ledend = MAXLEDLENGTH;

      for (int j = ledstart; j < ledend; j++)
      {
          leds[j].r = dmxChannels[groupChannelOffset+1];
          leds[j].g = dmxChannels[groupChannelOffset+2];
          leds[j].b = dmxChannels[groupChannelOffset+3];
      }
      break;

    case 2: // 234 RGB + 5 LENGTH + 6 OFFSET tapered
      ledlength = dmxChannels[groupChannelOffset+4];
      ledoffset = dmxChannels[groupChannelOffset+5];
      ledstart = ledoffset - ledlength;
      ledend = ledstart + ledlength;

      ledlength = ledend - ledstart;
      ledmiddle = ledstart + (ledlength / 2);
      ledDimmerIncrement = 1.0 / (double)(ledlength / 2);

      ledDimmer = 0;
      for (int j = ledstart; j < ledmiddle; j++)
      {
          ledDimmer += ledDimmerIncrement;
          if (j < 0 || j > MAXLEDLENGTH)
            continue;
          leds[j].r = dmxChannels[groupChannelOffset+1] * ledDimmer;
          leds[j].g = dmxChannels[groupChannelOffset+2] * ledDimmer;
          leds[j].b = dmxChannels[groupChannelOffset+3] * ledDimmer;
      }

      ledDimmer = 1 + ledDimmerIncrement;
      for (int j = ledmiddle; j < ledend; j++)
      {
          ledDimmer -= ledDimmerIncrement;
          if (j < 0 || j > MAXLEDLENGTH)
            continue;
          leds[j].r = dmxChannels[groupChannelOffset+1] * ledDimmer;
          leds[j].g = dmxChannels[groupChannelOffset+2] * ledDimmer;
          leds[j].b = dmxChannels[groupChannelOffset+3] * ledDimmer;
      }
      break;

    case 3: // 234 RGB + 5 LENGTH + 6 OFFSET + 7 OFFSET TUBE
      ledlength = dmxChannels[groupChannelOffset+4];
      ledoffset = dmxChannels[groupChannelOffset+5] + (dmxChannels[groupChannelOffset+6] * setupTubeNumber);
      ledstart = ledoffset - ledlength;
      ledend = ledstart + ledlength;
      if (ledstart < 0)
          ledstart = 0;
      if (ledend > MAXLEDLENGTH)
          ledend = MAXLEDLENGTH;

      for (int j = ledstart; j < ledend; j++)
      {
          leds[j].r = dmxChannels[groupChannelOffset+1];
          leds[j].g = dmxChannels[groupChannelOffset+2];
          leds[j].b = dmxChannels[groupChannelOffset+3];
      }
      break;

    case 4: // 234 RGB + 5 LENGTH + 66 OFFSET + 7 OFFSET TUBE (tapered)
      ledlength = dmxChannels[groupChannelOffset+4];
      ledoffset = dmxChannels[groupChannelOffset+5] + (dmxChannels[groupChannelOffset+6] * setupTubeNumber);
      ledstart = ledoffset - ledlength;
      ledend = ledstart + ledlength;

      ledlength = ledend - ledstart;
      ledmiddle = ledstart + (ledlength / 2);
      ledDimmerIncrement = 1.0 / (double)(ledlength / 2);

      ledDimmer = 0;
      for (int j = ledstart; j < ledmiddle; j++)
      {
          ledDimmer += ledDimmerIncrement;
          if (j < 0 || j > MAXLEDLENGTH)
            continue;
          leds[j].r = dmxChannels[groupChannelOffset+1] * ledDimmer;
          leds[j].g = dmxChannels[groupChannelOffset+2] * ledDimmer;
          leds[j].b = dmxChannels[groupChannelOffset+3] * ledDimmer;
      }

      ledDimmer = 1 + ledDimmerIncrement;
      for (int j = ledmiddle; j < ledend; j++)
      {
          ledDimmer -= ledDimmerIncrement;
          if (j < 0 || j > MAXLEDLENGTH)
            continue;
          leds[j].r = dmxChannels[groupChannelOffset+1] * ledDimmer;
          leds[j].g = dmxChannels[groupChannelOffset+2] * ledDimmer;
          leds[j].b = dmxChannels[groupChannelOffset+3] * ledDimmer;
      }
      break;

    case 5: // individual rgb 234 567 ... // pas encore implémenté en group mode
      for (int j = 0; j < MAXLEDLENGTH * 3; j += 3)
      {
          leds[j / 3].r = dmxChannels[ledoffset + j + 1];
          leds[j / 3].g = dmxChannels[ledoffset + j + 2];
          leds[j / 3].b = dmxChannels[ledoffset + j + 3];
      }
      break;

    case 7: // Mode 7 : FX1 Segments montant : 6 canaux par groupe : 234 RGB + 5 longueur segment + 6 longueur espace (pixels éteints entre deux segments) + 7 speed (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
      ir = dmxChannels[groupChannelOffset+1];
      ig = dmxChannels[groupChannelOffset+2];
      ib = dmxChannels[groupChannelOffset+3];
      onLength = dmxChannels[groupChannelOffset+ 4];
      offLength = dmxChannels[groupChannelOffset+5];
      fxSpeed = dmxChannels[groupChannelOffset+ 6] - 128;
      // onLength = 10;
      // offLength = 5;
      // ir=205;
      // ig=90;
      // ib=0;
      for (int j = 0; j < MAXLEDLENGTH; j++)
      {

          if (onLength-- > 0)
          {
            ledsTemp[j][0] = ir;
            ledsTemp[j][1] = ig;
            ledsTemp[j][2] = ib;
          }
          else if (offLength-- > 0)
          {
            ledsTemp[j][0] = 0;
            ledsTemp[j][1] = 0;
            ledsTemp[j][2] = 0;
          }
          else
          {
            onLength = dmxChannels[groupChannelOffset+ 4];
            offLength = dmxChannels[groupChannelOffset+ 5];
          }
      }

      intoffset = (int)mrdoublemodulo(lastOffset, (double)MAXLEDLENGTH);

      for (int j = 0; j < MAXLEDLENGTH; j++)
      {
          leds[(j + intoffset) % MAXLEDLENGTH].r = ledsTemp[j][0];
          leds[(j + intoffset) % MAXLEDLENGTH].g = ledsTemp[j][1];
          leds[(j + intoffset) % MAXLEDLENGTH].b = ledsTemp[j][2];
      }
      lastOffset = lastOffset + (fxSpeed * offsetInc);

      break;

    case 8: // Mode 8 : FX2 Segments montant R G B : 13 canaux par groupe : 234 RGB + 5 6 7 longueur segment rgb + 8 9 10 longueur espace rgb (pixels éteints entre deux segments) + 11 12 13 speed r g b (0 = vitesse max vers le bas, 128 = arrêt, 255 = vitesse max vers le haut)
      ir = dmxChannels[groupChannelOffset+ 1];
      ig = dmxChannels[groupChannelOffset+ 2];
      ib = dmxChannels[groupChannelOffset+ 3];
      onLengthR = dmxChannels[groupChannelOffset+ 4];
      onLengthG = dmxChannels[groupChannelOffset+ 5];
      onLengthB = dmxChannels[groupChannelOffset+ 6];
      offLengthR = dmxChannels[groupChannelOffset+ 7];
      offLengthG = dmxChannels[groupChannelOffset+8];
      offLengthB = dmxChannels[groupChannelOffset+ 9];
      fxSpeedR = dmxChannels[groupChannelOffset+10] - 128;
      fxSpeedG = dmxChannels[groupChannelOffset+ 11] - 128;
      fxSpeedB = dmxChannels[groupChannelOffset+ 12] - 128;

      for (int j = 0; j < MAXLEDLENGTH; j++)
      {

          if (onLengthR-- > 0)
          {
            ledsTemp[j][0] = ir;
          }
          else if (offLengthR-- > 0)
          {
            ledsTemp[j][0] = 0;
          }
          else
          {
            onLengthR = dmxChannels[groupChannelOffset+ 4];
            offLengthR = dmxChannels[groupChannelOffset+ 7];
          }
      }

      for (int j = 0; j < MAXLEDLENGTH; j++)
      {

          if (onLengthG-- > 0)
          {
            ledsTemp[j][1] = ig;
          }
          else if (offLengthG-- > 0)
          {
            ledsTemp[j][1] = 0;
          }
          else
          {
            onLengthG = dmxChannels[groupChannelOffset+ 5];
            offLengthG = dmxChannels[groupChannelOffset+ 8];
          }
      }

      for (int j = 0; j < MAXLEDLENGTH; j++)
      {

          if (onLengthB-- > 0)
          {
            ledsTemp[j][2] = ib;
          }
          else if (offLengthB-- > 0)
          {
            ledsTemp[j][2] = 0;
          }
          else
          {
            onLengthB = dmxChannels[groupChannelOffset+ 6];
            offLengthB = dmxChannels[groupChannelOffset+ 9];
          }
      }

      intoffset = (int)mrdoublemodulo(lastOffsetR, (double)MAXLEDLENGTH);
      for (int j = 0; j < MAXLEDLENGTH; j++)
      {
          leds[(j + intoffset) % MAXLEDLENGTH].r = ledsTemp[j][0];
      }
      lastOffsetR = lastOffsetR + (fxSpeedR * offsetIncR);

      intoffset = (int)mrdoublemodulo(lastOffsetG, (double)MAXLEDLENGTH);
      for (int j = 0; j < MAXLEDLENGTH; j++)
      {
          leds[(j + intoffset) % MAXLEDLENGTH].g = ledsTemp[j][1];
      }
      lastOffsetG = lastOffsetG + (fxSpeedG * offsetIncG);

      intoffset = (int)mrdoublemodulo(lastOffsetB, (double)MAXLEDLENGTH);
      for (int j = 0; j < MAXLEDLENGTH; j++)
      {
          leds[(j + intoffset) % MAXLEDLENGTH].b = ledsTemp[j][2];
      }
      lastOffsetB = lastOffsetB + (fxSpeedB * offsetIncB);

      break;

    default:
      break;
    }
    break;

  case 255: // affichage du numéro de groupe
    for (int j = 0; j < setupTubeNumber; j++)
    {
      leds[10 * j].r = 255;
      leds[10 * j].g = 0;
      leds[10 * j].b = 0;
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

void ledProgress(int count, int blinkspeed)
{

  for (int i = 0; i < count; i++)
  {
    FastLED.clear();
    FastLED.show();
    delay(blinkspeed);

    leds[i].r = 200;
    leds[i].g = 0;
    leds[i].b = 20;

    FastLED.show();
    delay(blinkspeed);
  }
}

void setup() {

  
  Serial.begin(115200);
  Serial.println("");
  Serial.print("Version ");
  Serial.println(VERSION);


  pinMode(D5,OUTPUT);
  digitalWrite(D5,LOW);// on utilise D5 comme GND pour le bouton 1
  
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH);  // GRB ordering is typical  

  // affichage de la version (blink)
  //ledProgress(VERSION,100);


  if(digitalRead(D1))// démarrage en mode DMX (on n'appuye pas sur le bouton au démarrage)
  {
    runningMode = DMXMODE;
    Serial.println("RUNNING MODE = DMX");
    

    //pinMode(LED_BUILTIN,OUTPUT);
  
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

    esp_now_register_recv_cb(OnDataRecv);

  }
  else // démarrage en mode ArtNet (le bouton est enfoncé au démarrage)
  {
    runningMode = ARTNETMODE;
    Serial.println("RUNNING MODE = ARTNET");

     FastLED.clear();
    for(int j=0;j<10;j++)
    {
      leds[j].r=0;
      leds[j].g=150;
      leds[j].b=150;
    }
    FastLED.show();
    delay(3000);
    
    if(digitalRead(D1)) // si on relache le bouton : autoConnect
    {
      // wifiManager.autoConnect(APNAME);
      WiFi.begin("OpenPoulpy", "youhououhou");
          FastLED.clear();
          int tentatives = 0;
      while (WiFi.status() != WL_CONNECTED)
      {
          delay(1000);
          leds[5*tentatives].b=150;
           FastLED.show();
          Serial.print(".");
          
          if (tentatives > 20)
          {            
            break;
          }
          tentatives++;
      }
      if (WiFi.status() != WL_CONNECTED)
      {
         String ssid = "ESP" + String(ESP.getChipId());
      
          wifiManager.autoConnect(ssid.c_str(), NULL);
      }

      // affichage leds jaune, 
      for(int j=0;j<30;j++)
      {
      leds[j].r=250;
      leds[j].g=100;
      leds[j].b=0;
      }
      FastLED.show();
      delay(2000);
      // si on appuye sur le bouton -> update firmware
      if(!digitalRead(D1))updateFirmware();

    }
    else // si on maintient le bouton : choix du réseau wifi
    {
      for(int j=0;j<15;j++)
      {
      leds[j].r=0;
      leds[j].g=150;
      leds[j].b=0;
      }
      FastLED.show();
      wifiManager.startConfigPortal();
      updateFirmware();
    }

    

  //  delay(5000);
    
    
  }
  
  

    // link the button 1 functions.    
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  
  
  
EEPROM.begin(EEPROM_SIZE);
//uint eeAddress=0;

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

  


  

   artnet.setArtDmxFunc([](DMX_FUNC_PARAM){
  
  for(int i=0;i<length;i++)
  {
    dmxChannels[i]=data[i];
  }

  
  });

  artnet.begin();
}

void loop() {
  button1.tick();

  
  if(etat==RUNNING)
  {    
    if(runningMode==ARTNETMODE)artnet.read();
    DMX2LEDSTRIP();  
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube  
  {
    FastLED.clear();
    if(setupTubeNumber==0)leds[0].r=250;// pour le groupe 0, on fait clignoter la première led en rouge
    else
    {
      for(int j=1;j<=10*setupTubeNumber;j+=10)
      {
      leds[j].r=0;
      leds[j].g=250;
      leds[j].b=0;
      }
    }
    
    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }



}


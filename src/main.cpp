/* 
version textile connecté
Ce code fait partie d'un système de contrôle DMX sans fil pour ledstrip.

Le système est composé d'un émetteur qui dispose d'une entrée DMX et transmet les valeurs des 512 canaux à tous les récepteurs du système.

Le code qui suit sert à programmer les récepteurs.

Les contrôleurs utilisés sont des ESP8266 (Wemos D1 mini) pour les récepteurs et un ESP32 pour l'émetteur mais l'un comme l'autre fonctionnent sur les cartes, il faut juste adapter quelques lignes pour utiliser
ESP-NOW. Il n'y a pas de raison particulière à ce que l'émetteur utilise un ESP32 (je crois que c'est juste ce que j'avais sous la main quand je l'ai programmé)

Il utilise un bouton pour pouvoir régler le numéro de groupe (voir modes de programmation) mais on peut très bien modifier quelques lignes pour que le numéro de groupe soit fixe

L'émetteur envoie 512 canaux DMX (par paquets de 128 canaux) et peut être décalé (il peut commencer à l'adresse 1 ou à n'importe quelle autre adresse). Si les adresses sont décalées,
cela est totalement transparent du point de vue du récepteur. Quand on parle de la première adresse dans le code du récepteur, il s'agit de la première adresse des 512 valeurs qu'il reçoit, 
qu'importe le vrai canal DMX auquel cela correspond.

Quelques modes ont été programmés mais vu que j'utilise pratiquement uniquement le mode 6 (chaque groupe contrôlé sur trois canaux, en rgb), je n'ai pas passé un temps dingue là-dessus.
Pour plus de fonctionnalités, programmez vos propre modes dans la fonction DMX2LEDSTRIP

Fonctionnement :
Le canal DMX 1 précise le mode:
Mode 0 : tous les pixels prennent la même valeur rgb représentée dans les canaux 2 3 4
Mode 1 : tirets défilants. Canaux 1 COLOR (codée sur un byte) + 2 DIMMER + 3 SPEED
Mode 2 3 4 : idem avec des segments de plus en plus longts
Mode 5 : pixels individuels. Les deux premiers canaux permettent de contrôler des groupes de pixels et de les espacer. 1 pixellength + 2 pixelgap + 3 dimmer + 567 rgb pixel1 + 8 9 10 rgb pixel2 ...
Mode 6 : chaque groupe utilise un triplet de valeurs RGB pour tous ses pixels (par exemple, le groupe 7 utilisera les valeurs rgb représentée par les canaux 19 20 21)
Mode 255 : affiche le numéro de groupe


SETUP (clic long pour y accéder ou en sortir) : réglage du numéro de groupe
Le nombre de LEDS correspondant au numéro de groupe clignote
Le numéro de groupe est enregistré en EEPROM
*/


#define BUTTONLESS true
#define DATA_PIN D2        // pin de contrôle du strip led
#define BUTTONPIN D1       // on définit le pin positif du bouton (il s'agit d'un pullup, quand le bouton est relevé, la valeur du pin est HIGH, quand le bouton est enfoncé, le contact au GND est fait et la valeur est donc LOW)
#define BUTTONGROUNDPIN D5 // pour faciliter le montage, on utilise une pin pour fournir le GND au bouton

#define MAXLEDLENGTH 144 // longueur du strip led // en général, la longueur n'a pas particulièrement d'influence sur la latence du contrôleur mais ça peut être utile de la régler pour les programmes qui font le "tour"
                         // du strip led (comme des segments de leds qui vont de bas en haut par exemple) ou pour être sûr de ne pas demander plus de courant que ce que l'alimentation prévue ne peut fournir

// #define MAXLEDLENGTH 10 // durant la programmation du code, pour les essais, si le ledstrip est alimenté via l'ESP, on se limite à quelques leds (pour ne pas le brûler)

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
WiFiManager wifiManager;
#define APNAME "mrLEDTUBE15"
#define VERSION 15

#define EEPROM_SIZE 32

#include <FastLED.h>

CRGB leds[MAXLEDLENGTH];
uint8_t ledsTemp[MAXLEDLENGTH][3];

#include "OneButton.h"
OneButton button1(BUTTONPIN, true);
// Setup a new OneButton on pin BUTTONPIN.

#include <ESP8266WiFiMulti.h>
#include <espnow.h>

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

bool etat = RUNNING; // etat peut être en mode SETUP ou RUNNING : pour entrer ou sortir du SETUP, il faut appuyer longuement sur le bouton, le ledstrip affiche alors le numéro n du groupe en allumant n leds vertes
                     // on peut changer de groupe en appuyant  sur le bouton, on incrémente le numéro de groupe jusque la limite (fixée à 10 pour le moment) au delà de laquelle on recommence au groupe 0
                     // le groupe 0 conrrespond à 1 led rouge pour le distinguer du groupe 1 (une led verte)

int flashInterval;

uint8_t dmxChannels[512]; // tableau dans lequel seront stockées les valeurs des 512 canaux DMX

typedef struct struct_dmx_packet
{
  uint8_t blockNumber; // on divise les 512 adresses en 4 blocs de 128 adresses (on ne peut pas tout envoyer en une fois car la taille maximale des packets transmis par ESP-NOW est limitée à 250 bytes)
  uint8_t dmxvalues[128];
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;

void OnDataSent(u8 *mac_addr, u8 status) {} // la fonction OnDataSent doit être déclarée mais concrètement on n'en a pas besoin (pour l'instant, aucune donnée n'est renvoyée par les récepteurs à l'émetteur)

// Callback when data is received
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));
  uint8_t packetNumber = incomingDMXPacket.blockNumber;
  for (int i = 0; i < 128; i++)
  {
    dmxChannels[(packetNumber * 128) + i] = incomingDMXPacket.dmxvalues[i];
  }
}

double mrdoublemodulo(double nombre, double diviseur) // je ne sais plus pourquoi j'avais besoin d'une opération modulo sur des doubles, ce qui n'existe pas de base, apparemment
{
  while (nombre < 0)
    nombre += diviseur;
  while (nombre >= diviseur)
    nombre -= diviseur;
  return nombre;
}

struct Color
{ // type utilisé par la fonction convertToColor
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

Color convertToColor(uint8_t index)
{ // fonction permettant de mapper 256 couleurs de manière continue sur 255 valeurs (pour pouvoir utilier un seul canal DMX pour la couleur dans certains modes)
  Color color;

  if (index < 85)
  {
    color.r = index * 3;
    color.g = 255 - index * 3;
    color.b = 0;
  }
  else if (index < 170)
  {
    index -= 85;
    color.r = 255 - index * 3;
    color.g = 0;
    color.b = index * 3;
  }
  else
  {
    index -= 170;
    color.r = 0;
    color.g = index * 3;
    color.b = 255 - index * 3;
  }

  return color;
}

// Contrôle du ledstrip proprement dit, en fonction des valeurs DMX
/*
Remarque : le tableau contenant les valeurs "dmxChannels" est indexé de 0 à 511 alors que les canaux DMX sont numérotés de 1 à 512. C'est la faute des créateurs du DMX, pas la mienne
Le canal 1 règle le mode de fonctionnement
Mode 0 : tous les pixels prennent la même valeur rgb représentée dans les canaux 2 3 4
Mode 1 : tirets défilants. Canaux 1 COLOR (codée sur un byte) + 2 DIMMER + 3 SPEED
Mode 2 3 4 : idem avec des segments de plus en plus longts
Mode 5 : pixels individuels. Les deux premiers canaux permettent de contrôler des groupes de pixels et de les espacer. 1 pixellength + 2 pixelgap + 3 dimmer + 567 rgb pixel1 + 8 9 10 rgb pixel2 ...
Mode 6 : chaque groupe utilise un triplet de valeurs RGB pour tous ses pixels (par exemple, le groupe 7 utilisera les valeurs rgb représentée par les canaux 19 20 21)
Mode 255 : affiche le numéro de groupe

*/

void DMX2LEDSTRIP()
{

  FastLED.clear();

  setupMode = dmxChannels[0];

  double ledDimmer;
  double ledDimmerIncrement;
  int onLength;
  int offLength;
  int intoffset;
  int fxSpeed;

  int pixelLength = 1;
  int pixelGap = 0;

  int ir = 3 * setupTubeNumber + 1;
  int ig = 3 * setupTubeNumber + 2;
  int ib = 3 * setupTubeNumber + 3;

  int segmentLength;

  Color rgbcolor;

  switch (setupMode)
  {
  case 0: // 123 RGB for all strip at once

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  case 1: // 1 COLOR + 2 DIMMER + 3 SPEED (tapered)
  case 2:
  case 3:
  case 4:
    rgbcolor = convertToColor(dmxChannels[1]);

    ledDimmer = (double(1) / double(255)) * (double)(dmxChannels[2]);

    segmentLength = setupMode * 8;

    ir = rgbcolor.r * ledDimmer;
    ig = rgbcolor.g * ledDimmer;
    ib = rgbcolor.b * ledDimmer;

    onLength = segmentLength;
    offLength = segmentLength;

    fxSpeed = 16 * sin((double(1) / double(255)) * (double)(dmxChannels[3]) * 2 * PI);
    ledDimmerIncrement = 1.0 / (double)(segmentLength) / 2.0;

    ledDimmer = 0;

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {

      if (onLength-- > 0)
      {
        ledsTemp[j][0] = ir * ledDimmer;
        ledsTemp[j][1] = ig * ledDimmer;
        ledsTemp[j][2] = ib * ledDimmer;
        ledDimmer = ledDimmer + ledDimmerIncrement;
        if (ledDimmer >= 1)
        {
          ledDimmer = 1;
          ledDimmerIncrement = -1.0 / (double)(segmentLength) / 2.0;
        }
        else if (ledDimmer <= 0)
        {
          ledDimmer = 0;
        }
      }
      else if (offLength-- > 0)
      {
        ledsTemp[j][0] = 0;
        ledsTemp[j][1] = 0;
        ledsTemp[j][2] = 0;
      }
      else
      {
        onLength = segmentLength;
        offLength = segmentLength;
        ledDimmer = 0;
        ledDimmerIncrement = 1.0 / (double)(segmentLength) / 2.0;
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

  case 5: // individual rgb : 1 pixellength + 2 pixelgap + 3 dimmer + 567 rgb pixel1 + 8 9 10 rgb pixel2 ...

    pixelLength = dmxChannels[1];
    pixelGap = dmxChannels[2];
    ledDimmer = (double)dmxChannels[3] / (double)255;

    for (int j = 0; j < (3 * MAXLEDLENGTH) / (pixelLength + pixelGap); j += 3)
    {
      for (int pix = 0; pix < pixelLength; pix++)
      {
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].r = dmxChannels[j + 4] * ledDimmer;
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].g = dmxChannels[j + 5] * ledDimmer;
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].b = dmxChannels[j + 6] * ledDimmer;
      }
      for (int pix = pixelLength; pix < pixelGap + pixelLength; pix++)
      {
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].r = 0;
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].g = 0;
        leds[((pixelLength + pixelGap) * (j / 3)) + pix].b = 0;
      }
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

void click1()
{   
  // incrémente le numéro de groupe
  if (etat == RUNNING)
    return; // en mode RUNNING, on ignore cette action
  setupTubeNumber = (setupTubeNumber + 1) % 10;
}

void longPressStart1()
{
  Serial.print("longpress | etat = ");
  Serial.println((etat ? "RUNNING" : "SETUP"));

  if (etat == SETUP) // avant de sortir du SETUP, on enregistre les données en mémoire persistante
  {
    EEPROM.write(0, setupAddress);
    EEPROM.write(4, setupMode);
    EEPROM.write(8, setupTubeNumber);
    Serial.print("Commit =  ");
    Serial.println(EEPROM.commit());
  }

  etat = !etat; // on passe de RUNNING à SETUP ou inversement
}

void setup()
{

  Serial.begin(115200);
  Serial.println("");
  Serial.print("Version ");
  Serial.println(VERSION);

  pinMode(BUTTONGROUNDPIN, OUTPUT);
  digitalWrite(BUTTONGROUNDPIN, LOW); // on utilise BUTTONGROUNDPIN comme GND pour le bouton 1

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH); // GRB ordering is typical

  WiFi.disconnect();
  ESP.eraseConfig();

  // Wifi STA Mode
  WiFi.mode(WIFI_STA);
  // Get Mac Address
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("\nESP-Now Receiver");

  // Initializing the ESP-NOW
  if (esp_now_init() != 0)
  {
    Serial.println("Problem during ESP-NOW init");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // link the button 1 functions.
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  EEPROM.begin(EEPROM_SIZE);

  setupAddress = EEPROM.read(0);
  setupMode = EEPROM.read(4);
  setupTubeNumber = EEPROM.read(8);
  if ((setupAddress < 1) || (setupAddress > 512))
    setupAddress = 1;
  if ((setupMode < 1) || (setupMode > 255))
    setupMode = 1;
  if ((setupTubeNumber < 0) || (setupTubeNumber > 32))
    setupTubeNumber = 0;

  // EEPROM.end();

  Serial.print(" ");
  Serial.print("Paramètres récupérés en EEPROM : Addresse = ");
  Serial.print(setupAddress);
  Serial.print(" | Mode = ");
  Serial.print(setupMode);
  Serial.print(" | Tube Group = ");
  Serial.println(setupTubeNumber);
}

void loop()
{
  if(!BUTTONLESS)button1.tick();

  if (etat == RUNNING)
  {
    DMX2LEDSTRIP();
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube
  {
    FastLED.clear();
    if (setupTubeNumber == 0)
      leds[0].r = 250; // pour le groupe 0, on fait clignoter la première led en rouge
    else
    {
      for (int j = 1; j <= 10 * setupTubeNumber; j += 10)
      {
        leds[j].r = 0;
        leds[j].g = 250;
        leds[j].b = 0;
      }
    }

    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }
}

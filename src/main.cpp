/*// - - - - -
Récepteur DMX sans fil basé sur les récepteurs pour les stripleds
Wiring :
WEMOS D1 ---- MAW485 :
RO : /
DI (data in) : entrée connectée à D4 (ATTENTION!!! D4 est le pin par défaut avec cette librairie, pas TX!!!)
RE et DE (receiver enable et driver enable) : HIGH pour sortie DMX (5V)
VCC - 5V
GND - GND
XLR1 : GND (shield)
XLR2 : B (fil rouge)
XLR3 : A (fil blanc)
*/

#include <Arduino.h>
#include <ESPDMX.h>
DMXESPSerial dmx;

#include <EEPROM.h>
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <ESP8266WiFiMulti.h>
#include <espnow.h>

uint8_t dmxChannels[512];

typedef struct struct_dmx_packet
{
  uint8_t blockNumber; // on divise les 512 adresses en 4 blocs de 128 adresses
  uint8_t dmxvalues[128];
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;

void OnDataSent(u8 *mac_addr, u8 status) {} // doit être déclarée mais on ne l'utilise pas

// Callback when data is received
// Je pourrais envoyer du DMX directement à partir d'ici mais pour différentes raisons, je préfère passer par le tableau intermédiaire dmxChannels
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));
  uint8_t packetNumber = incomingDMXPacket.blockNumber;

  for (int i = 0; i < 128; i++)
  {
    dmxChannels[(packetNumber * 128) + i] = incomingDMXPacket.dmxvalues[i];
  }
}

void setup()
{
  dmx.init(512);

  WiFi.disconnect();
  ESP.eraseConfig();

  // Wifi STA Mode
  WiFi.mode(WIFI_STA);

  // Initializing the ESP-NOW
  if (esp_now_init() != 0)
  {
    // message d'erreur
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
}

void loop()
{

  for (int i = 0; i < 511; i++) // je ne remplis pas le canal 512, ça fait  planter le truc et je n'ai pas envie de chercher pourquoi
  {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

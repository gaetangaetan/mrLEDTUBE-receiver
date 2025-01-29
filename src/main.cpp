/***********************************************************************
 Ce code reçoit des données Art-Net (UDP) 
 et les transmets en DMX via une sortie XLR
************************************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>


// Bibliothèque Art-Net 
#include <ArtnetWifi.h>

// Bibliothèque DMX 
#include <ESPDMX.h>
DMXESPSerial dmx;

/***********************************************************************
 *                          DEFINES / CONST
 ***********************************************************************/

// Broches
// #define DATA_PIN            D2    // pin de contrôle du strip led
#define BUTTONPIN           D1    // bouton (pullup interne)
#define BUTTONGROUNDPIN     D5    // on s'en sert pour fournir GND au bouton

#define VERSION             161


/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

// Tableau qui stocke les 512 canaux DMX (index 0=canal 1 DMX)
uint8_t dmxChannels[512];

// WiFi manager
WiFiManager wifiManager;

// Objet Art-Net
ArtnetWifi artnet;


/***********************************************************************
 *                             FONCTIONS
 ***********************************************************************/

// Callback Art-Net : chaque fois qu'on reçoit un packet DMX
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  // Ici, 'data' contient 'length' octets de données DMX pour l'univers 'universe'.
  // On recopie dans dmxChannels (max 512 canaux).
  // Si vous ne gérez qu'un seul univers, c'est facile. 
  // S'il y a plusieurs univers, vous pouvez gérer un offset : 
  //   e.g. if universe == 0 => offset 0 
  //        if universe == 1 => offset 512, etc.
  // Pour un récepteur unique, on suppose un seul univers DMX (0 ou 1).

  // Simple exemple : univers = 0
  int offset = 0; 
  // ou si vous voulez "universe 1" => offset=0, 
  //   => if(universe==1) offset=0; 
  //   => if(universe==2) offset=512; (pour 2 univers)
  // etc.

  for (int i = 0; i < length; i++) {
    // Ne pas dépasser 512
    if (i + offset < 512) {
      dmxChannels[i + offset] = data[i];
    }
  }
}

/***********************************************************************
 *                              SETUP
 ***********************************************************************/ 
void setup()
{

  dmx.init(512);
  // randomHueOffset = rand()%256;
  Serial.begin(115200);
  Serial.println("\n=== Interface ArtNet DMX ===");
  Serial.print("Version ");
  Serial.println(VERSION);

    
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(true);

  if(false) {
    // 2.a) Lancement direct du config portal
    // clignotement bleu => 3 blinks, 1 seconde
    // blinkLeds(3, 1000, 0,0,255);
    Serial.println("Démarrage du portail WiFiManager (FORCE)");
    wifiManager.startConfigPortal("Artnet-Receiver"); 
    // => Bloque jusqu’à ce qu’on ait configuré un réseau. 
  } else {
    // 2.b) On tente autoConnect => 
    //    si pas de réseau connu -> ouvre un portail 
    //    sinon connecte direct
    if(!wifiManager.autoConnect("Artnet-Receiver")) {
      // si on sort d’autoconnect sans succès => 
      //   on peut re-tenter ou faire un reset
      Serial.println("Echec d’autoConnect, on lance un portail quand même");
      // blinkLeds(3, 1000, 0,0,255);
      wifiManager.startConfigPortal("Artnet-Receiver"); 
    }
  }

  // A ce stade, on est connecté ou on a configuré un nouveau wifi
  // => suite
  Serial.print("WiFi connecté, IP=");
  Serial.println(WiFi.localIP());

  // ================ Démarrage de l’Art-Net ================
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);

  // Fin du setup
}


/***********************************************************************
 *                              LOOP
 ***********************************************************************/
void loop()
{
  // Lire les packets Art-Net
  artnet.read();

  for (int i = 0; i < 511; i++) // je ne remplis pas le canal 512, ça fait  planter le truc et je n'ai pas envie de chercher pourquoi
  {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
  
}

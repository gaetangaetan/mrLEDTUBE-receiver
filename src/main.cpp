/***********************************************************************
 * Ce code reçoit des données Art-Net (UDP) 
 * et les transmets en DMX via une sortie XLR (MAX485)
 * Ajout d'un serveur Web affichant l'IP, accessible via http://dmx.local
 ************************************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>       // Pour le service mDNS (dmx.local)

// Bibliothèque Art-Net 
#include <ArtnetWifi.h>

// Bibliothèque DMX 
#include <ESPDMX.h>
DMXESPSerial dmx;

/***********************************************************************
 *                          DEFINES / CONST
 ***********************************************************************/

// Broches
// #define DATA_PIN            D2    // pin de contrôle éventuelle pour LED
#define BUTTONPIN           D1      // bouton (pullup interne)
#define BUTTONGROUNDPIN     D5      // sert à fournir GND au bouton

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

// Objet serveur HTTP (port 80)
ESP8266WebServer server(80);


/***********************************************************************
 *                             FONCTIONS
 ***********************************************************************/

// Callback Art-Net : chaque fois qu'on reçoit un paquet DMX
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  // 'data' contient 'length' octets de données DMX pour l'univers 'universe'.
  // Ici on suppose qu'on ne gère qu'un seul univers (universe = 0 ou 1).
  
  int offset = 0; 
  // Si vous gérez plusieurs univers, vous pouvez adapter l’offset 
  // (par ex. universe 0 => offset 0, universe 1 => offset 512, etc.)

  for (int i = 0; i < length; i++) {
    if (i + offset < 512) {
      dmxChannels[i + offset] = data[i];
    }
  }
}

/**
 * Page principale du serveur web
 */
void handleRoot() {
  // Construction d'une page HTML simple
  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title></head><body>");
  page += F("<h1>Interface ArtNet -> DMX</h1>");
  
  // Affichage de l'IP
  page += F("<p>Adresse IP de l'ESP8266 : ");
  page += WiFi.localIP().toString();
  page += F("</p>");

  // Exemple : on pourra ajouter plus d'infos plus tard
  page += F("<p>Version firmware : ");
  page += String(VERSION);
  page += F("</p>");

  page += F("</body></html>");

  // Envoi de la réponse au client
  server.send(200, "text/html", page);
}

/**
 * Page de gestion des URLs non définies (404)
 */
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}


/***********************************************************************
 *                              SETUP
 ***********************************************************************/ 
void setup()
{
  dmx.init(512);   // Initialisation de la bibliothèque DMX
  Serial.begin(115200);
  Serial.println("\n=== Interface ArtNet DMX ===");
  Serial.print("Version ");
  Serial.println(VERSION);

  // Configuration WiFi via WiFiManager
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(true);

  // autoConnect : tente de se connecter au réseau connu ou lance un portail
  if(!wifiManager.autoConnect("Artnet-Receiver")) {
    Serial.println("Echec d’autoConnect, on lance un portail");
    wifiManager.startConfigPortal("Artnet-Receiver"); 
  }

  Serial.print("WiFi connecté, IP=");
  Serial.println(WiFi.localIP());

  // --- Initialisation du mDNS pour accéder via http://dmx.local ---
  if (MDNS.begin("dmx")) {
    Serial.println("MDNS responder démarré => http://dmx.local");
  } else {
    Serial.println("Erreur lors du démarrage du mDNS");
  }

  // --- Configuration du serveur web ---
  server.on("/", handleRoot);          // page principale
  server.onNotFound(handleNotFound);   // page 404
  server.begin();
  MDNS.addService("http", "tcp", 80);  // annonce du service HTTP sur le port 80
  Serial.println("Serveur HTTP démarré.");

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
  // Met à jour le serveur MDNS
  MDNS.update();
  
  // Gère les requêtes HTTP entrantes
  server.handleClient();

  // Lire les paquets Art-Net
  artnet.read();

  // Mise à jour des canaux DMX
  // Remarque : on écrit ici 511 canaux (le 512e n'est pas forcément nécessaire).
  for (int i = 0; i < 511; i++) {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

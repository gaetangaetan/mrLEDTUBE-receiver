/***********************************************************************
 * Ce code reçoit des données Art-Net (UDP) 
 * et les transmets en DMX via une sortie XLR (MAX485)
 * Ajout d'un serveur Web affichant l'IP et permettant de reconfigurer le WiFi
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
#define BUTTONPIN           D1      // bouton (pullup interne)
#define BUTTONGROUNDPIN     D5      // sert à fournir GND au bouton

#define VERSION             162


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
  int offset = 0; 

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
  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title></head><body>");
  page += F("<h1>Interface ArtNet -> DMX</h1>");
  
  page += F("<p>Adresse IP de l'ESP8266 : ");
  page += WiFi.localIP().toString();
  page += F("</p>");

  page += F("<p>Version firmware : ");
  page += String(VERSION);
  page += F("</p>");

  // Bouton pour réinitialiser le WiFi
  page += F("<form action='/resetwifi' method='POST'>");
  page += F("<input type='submit' value='Reconfigurer le WiFi' ");
  page += F("style='padding:10px; font-size:16px; background:#f04; color:white; border:none; cursor:pointer;'/>");
  page += F("</form>");

  page += F("</body></html>");

  server.send(200, "text/html", page);
}

/**
 * Page pour réinitialiser WiFiManager et relancer la configuration
 */
void handleResetWiFi() {
  server.send(200, "text/plain", "Reinitialisation WiFi... Redémarrage de l'ESP...");
  delay(1000);
  wifiManager.resetSettings();  // Efface les paramètres WiFi
  ESP.restart();                // Redémarre l’ESP pour relancer WiFiManager
}

/**
 * Page 404
 */
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}


/***********************************************************************
 *                              SETUP
 ***********************************************************************/ 
void setup()
{
  dmx.init(512);
  Serial.begin(115200);
  Serial.println("\n=== Interface ArtNet DMX ===");
  Serial.print("Version ");
  Serial.println(VERSION);

  // Configuration WiFi via WiFiManager
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(true);

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
  server.on("/", handleRoot);          // Page principale
  server.on("/resetwifi", HTTP_POST, handleResetWiFi); // Page pour reset WiFi
  server.onNotFound(handleNotFound);   // Page 404
  server.begin();
  MDNS.addService("http", "tcp", 80);  // Annonce du service HTTP sur le port 80
  Serial.println("Serveur HTTP démarré.");

  // ================ Démarrage de l’Art-Net ================
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);
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
  for (int i = 0; i < 511; i++) {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

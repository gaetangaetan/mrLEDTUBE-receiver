/***********************************************************************
 * Interface Web pour un récepteur ArtNet -> DMX sur ESP8266 (Wemos D1 Mini)
 * - Affichage d'un tableau de 512 canaux DMX (20 colonnes × 26 lignes)
 * - Surlignage des valeurs modifiées (paramétrable)
 * - Changement de couleur du texte des valeurs égales à 0 (paramétrable)
 * - Slider permettant d'ajuster la vitesse de rafraîchissement (100ms à 2000ms)
 ************************************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// Bibliothèque Art-Net 
#include <ArtnetWifi.h>

// Bibliothèque DMX 
#include <ESPDMX.h>
DMXESPSerial dmx;

#define VERSION 172

// 🎨 Couleurs personnalisables
#define ZERO_COLOR "#f9f9f9"      // Couleur du texte pour valeurs DMX = 0
#define HIGHLIGHT_COLOR "#ffaae3" // Couleur de surlignage pour valeurs modifiées

// 📊 Dimensions du tableau DMX
#define NUM_COLS 20  // Nombre de colonnes
#define NUM_ROWS 26  // Nombre de lignes
#define MAX_CHANNELS 512  // Nombre maximal de canaux DMX

/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

uint8_t dmxChannels[MAX_CHANNELS];  
WiFiManager wifiManager;
ArtnetWifi artnet;
ESP8266WebServer server(80);

/***********************************************************************
 *                             FONCTIONS
 ***********************************************************************/

// Callback Art-Net : chaque fois qu'on reçoit un paquet DMX
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  for (int i = 0; i < length; i++) {
    if (i < MAX_CHANNELS) {
      dmxChannels[i] = data[i];
    }
  }
}

/**
 * Retourne les valeurs DMX en JSON pour mise à jour dynamique du tableau
 */
void handleDMXData() {
  String json = "[";
  for (int i = 0; i < MAX_CHANNELS; i++) {
    json += String(dmxChannels[i]);
    if (i < MAX_CHANNELS - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

/**
 * Page principale du serveur web
 */
void handleRoot() {
  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title>");
  page += F("<style>");
  page += F("body { font-family: Arial, sans-serif; text-align: center; }");
  page += F("table { border-collapse: collapse; margin: auto; }");
  page += F("td, th { border: 1px solid black; padding: 4px; width: 30px; text-align: center; transition: background 0.5s, color 0.5s; }");
  page += F("th { background-color: lightgray; }");
  page += F(".channel-index { font-weight: bold; background-color: #e0e0e0; }");
  page += F("</style>");
  page += F("</head><body>");
  
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

  // === Ajout du slider pour la vitesse de rafraîchissement ===
  page += F("<h2>Vitesse de mise à jour</h2>");
  page += F("<input type='range' id='refreshRate' min='100' max='2000' step='100' value='1000' oninput='updateRate()'>");
  page += F("<p>Rafraîchissement : <span id='rateValue'>1000</span> ms</p>");

  // === Tableau des valeurs DMX ===
  page += F("<h2>Valeurs DMX</h2>");
  page += F("<table id='dmxTable'><tr>");

  // En-têtes des colonnes (1 à 20)
  page += F("<th></th>");
  for (int i = 1; i <= NUM_COLS; i++) {
    page += "<th>" + String(i) + "</th>";
  }
  page += F("</tr>");

  // Génération du tableau avec index des canaux
  for (int row = 0; row < NUM_ROWS; row++) {
    page += "<tr>";
    int firstChannel = (row * NUM_COLS) + 1;

    // Ne pas afficher de numérotation si on dépasse 512 canaux
    if (firstChannel <= MAX_CHANNELS) {
      page += "<td class='channel-index'>" + String(firstChannel) + "</td>";
    } else {
      page += "<td></td>"; // Case vide
    }

    for (int col = 0; col < NUM_COLS; col++) {
      int index = row * NUM_COLS + col;
      if (index < MAX_CHANNELS) {
        page += "<td id='ch" + String(index) + "' style='color:" ZERO_COLOR ";'>0</td>";
      } else {
        page += "<td></td>"; // Cellules vides après 512
      }
    }
    page += "</tr>";
  }
  page += F("</table>");

  // === Script AJAX ===
  page += F("<script>");
  page += F("let dmxValues = new Array(512).fill(0);");
  page += F("let changeTimers = new Array(512).fill(null);");
  page += F("let refreshRate = 1000;");
  page += F("let interval = setInterval(updateDMX, refreshRate);");

  page += F("function updateDMX() {");
  page += F("fetch('/dmxdata')") ;
  page += F(".then(response => response.json())");
  page += F(".then(data => {");
  page += F("for (let i = 0; i < 512; i++) {");
  page += F("let cell = document.getElementById('ch' + i);");
  page += F("if (cell) {");
  page += F("if (data[i] !== dmxValues[i]) {");
  page += F("cell.style.backgroundColor = '" HIGHLIGHT_COLOR "';");
  page += F("clearTimeout(changeTimers[i]);");
  page += F("changeTimers[i] = setTimeout(() => { cell.style.backgroundColor = ''; }, 5000);");
  page += F("}");
  page += F("cell.innerText = data[i];");
  page += F("cell.style.color = (data[i] === 0) ? '" ZERO_COLOR "' : 'black';");
  page += F("dmxValues[i] = data[i];");
  page += F("}");
  page += F("}");
  page += F("})");
  page += F("}");
  page += F("</script>");

  page += F("</body></html>");

  server.send(200, "text/html", page);
}

/***********************************************************************
 *                              SETUP & LOOP
 ***********************************************************************/
void setup()
{
  dmx.init(512);
  Serial.begin(115200);
  
  if (!wifiManager.autoConnect("Artnet-Receiver")) {
    wifiManager.startConfigPortal("Artnet-Receiver"); 
  }

  if (MDNS.begin("dmx")) {
    Serial.println("MDNS actif => http://dmx.local");
  }

  server.on("/", handleRoot);
  server.on("/dmxdata", handleDMXData);
  server.begin();
  
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);
}

void loop()
{
  MDNS.update();
  server.handleClient();
  artnet.read();

  for (int i = 0; i < 511; i++) {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

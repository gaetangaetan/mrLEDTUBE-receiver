/***********************************************************************
 * Exemple : Récepteur ArtNet -> DMX sur ESP8266 + gestion de 8 presets
 *           - Mode LIVE (flux Artnet) ou PRESET (valeurs enregistrées)
 *           - 8 presets enregistrables / rappelables (stockés en EEPROM)
 *           - Le bouton "Reconfigurer le WiFi" relance le portail WiFiManager
 *           - Nouvelle fonctionnalité : Limiter l'affichage à un nombre de canaux (128, 256, 384, 512)
 ************************************************************************/

#include <Arduino.h>
#include <EEPROM.h>             // Ajout pour EEPROM
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// Art-Net
#include <ArtnetWifi.h>

// DMX
#include <ESPDMX.h>
DMXESPSerial dmx;

// --- Paramètres d'interface et de couleurs ---
#define ZERO_COLOR       "#888888"      // Couleur du texte pour valeurs DMX = 0
#define HIGHLIGHT_COLOR "#ffaae3"       // Couleur de surlignage pour valeurs modifiées

// --- Dimensions du tableau DMX ---
const int NUM_COLS = 20;  // 20 colonnes
int displayCount = 512;   // Nombre de canaux à afficher, par défaut 512

#define MAX_CHANNELS 512 // 512 canaux DMX

// --- Nombre de presets ---
#define PRESET_COUNT 8

// --- EEPROM ---
#define EEPROM_SIZE 4096                // 8 presets × 512 octets = 4096
// Optionnel : vous pourriez réserver un peu plus pour stocker une signature, etc.

#define VERSION 205

/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

bool isLive = true;                // Indique si on est en mode LIVE ou PRESET
uint8_t dmxChannels[MAX_CHANNELS]; // Valeurs envoyées en DMX
uint8_t artnetChannels[MAX_CHANNELS]; // Valeurs reçues en ArtNet (utilisées en mode LIVE)

WiFiManager wifiManager;
ArtnetWifi artnet;
ESP8266WebServer server(80);

/***********************************************************************
 *                            FONCTIONS EEPROM
 ***********************************************************************/

/**
 * Initialise l'EEPROM en lecture/écriture
 */
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);  // Réserve 4096 octets en flash pour émuler l'EEPROM
}

/**
 * Sauvegarde un preset en EEPROM à l'offset correspondant
 * param : presetIndex (0..7)
 * param : data (tableau de 512 octets) à écrire
 */
void savePresetToEEPROM(int presetIndex, const uint8_t* data) {
  int offset = presetIndex * MAX_CHANNELS;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    EEPROM.write(offset + i, data[i]);  
  }
  EEPROM.commit();  // Finalise l'écriture
}

/**
 * Charge un preset depuis l'EEPROM
 * param : presetIndex (0..7)
 * param : data (tableau de 512 octets) où stocker les valeurs lues
 */
void loadPresetFromEEPROM(int presetIndex, uint8_t* data) {
  int offset = presetIndex * MAX_CHANNELS;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    data[i] = EEPROM.read(offset + i);
  }
}

/***********************************************************************
 *                            FONCTIONS DMX
 ***********************************************************************/

// Callback Art-Net : reçoit un paquet DMX. On stocke dans artnetChannels si mode LIVE
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* packetData)
{
  if (isLive) {
    for (int i = 0; i < length; i++) {
      if (i < MAX_CHANNELS) {
        artnetChannels[i] = packetData[i];
      }
    }
  }
  // En mode PRESET, on ignore les données ArtNet
}

/***********************************************************************
 *                            ROUTES WEB
 ***********************************************************************/

/**
 * /dmxdata : retourne les valeurs dmxChannels en JSON (displayCount entiers)
 */
void handleDMXData() {
  String json = "[";
  for (int i = 0; i < displayCount; i++) {
    json += String(dmxChannels[i]);
    if (i < (displayCount - 1)) {
      json += ",";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

/**
 * /toggleMode : passe de LIVE à PRESET ou inversement
 */
void handleToggleMode() {
  isLive = !isLive;
  server.send(200, "text/plain", "Mode toggled");
}

/**
 * /save : enregistre les dmxChannels dans un preset, param 'p' = 1..8
 * Exemple : /save?p=3
 */
void handleSavePreset() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing preset number");
    return;
  }
  int presetIndex = server.arg("p").toInt() - 1; // 0..7
  if (presetIndex < 0 || presetIndex >= PRESET_COUNT) {
    server.send(400, "text/plain", "Invalid preset number");
    return;
  }
  // Ecrit dmxChannels (512 octets) en EEPROM
  savePresetToEEPROM(presetIndex, dmxChannels);

  server.send(200, "text/plain", "Preset saved in slot " + String(presetIndex+1));
}

/**
 * /recall : charge un preset dans dmxChannels, param 'p' = 1..8
 * Exemple : /recall?p=3
 */
void handleRecallPreset() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing preset number");
    return;
  }
  int presetIndex = server.arg("p").toInt() - 1;
  if (presetIndex < 0 || presetIndex >= PRESET_COUNT) {
    server.send(400, "text/plain", "Invalid preset number");
    return;
  }
  // Lit l'EEPROM et recopie dans dmxChannels
  loadPresetFromEEPROM(presetIndex, dmxChannels);

  server.send(200, "text/plain", "Preset recalled: " + String(presetIndex+1));
}

/**
 * /resetwifi : efface les paramètres WiFi et redémarre l'ESP pour relancer le portail WiFiManager
 */
void handleResetWiFi() {
  server.send(200, "text/plain", "Réinitialisation WiFi... Redémarrage de l'ESP...");
  delay(1000);
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * /setDisplay : définit le nombre de canaux à afficher
 * param 'count' = 128, 256, 384, 512
 * Exemple : /setDisplay?count=256
 */
void handleSetDisplay() {
  if (!server.hasArg("count")) {
    server.send(400, "text/plain", "Missing count parameter");
    return;
  }
  int count = server.arg("count").toInt();
  if (count != 128 && count != 256 && count != 384 && count != 512) {
    server.send(400, "text/plain", "Invalid count value");
    return;
  }
  displayCount = count;
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

/***********************************************************************
 *                            PAGE PRINCIPALE
 ***********************************************************************/

void handleRoot() {
  String modeString = isLive ? "LIVE" : "PRESET";

  // Calculer le nombre de lignes en fonction de displayCount et NUM_COLS
  int numRows = (displayCount + NUM_COLS - 1) / NUM_COLS;

  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title>");
  page += F("<style>");
  page += F("body { font-family: Arial, sans-serif; text-align: center; }");
  page += F("table { border-collapse: collapse; margin: auto; }");
  page += F("td, th { border: 1px solid black; padding: 4px; width: 30px; text-align: center; transition: background 0.5s, color 0.5s; }");
  page += F("th { background-color: lightgray; }");
  page += F(".channel-index { font-weight: bold; background-color: #e0e0e0; }");
  page += F(".btn { padding:6px 10px; margin:2px; cursor:pointer;}"); // style commun
  page += F("</style>");
  page += F("</head><body>");

  page += F("<h1>Interface ArtNet -> DMX + PRESSETS</h1>");

  // Informations WiFi et Firmware
  page += F("<p>Adresse IP de l'ESP8266 : ");
  page += WiFi.localIP().toString();
  page += F("</p>");
  page += F("<p>Version firmware : ");
  page += String(VERSION);
  page += F("</p>");

  // Bouton pour reconfigurer le WiFi (relancer portail WiFiManager)
  page += F("<form action='/resetwifi' method='POST'>");
  page += F("<input type='submit' class='btn' value='Reconfigurer le WiFi' style='background:#f04; color:white;'/>");
  page += F("</form>");

  // Indication du mode actuel + bouton de basculement
  page += F("<h2>Mode actuel : <span id='mode'>");
  page += modeString;
  page += F("</span></h2>");
  page += F("<button class='btn' onclick=\"fetch('/toggleMode').then(r=>location.reload());\">Toggle Mode</button>");

  // Menu déroulant pour choisir le nombre de canaux à afficher
  page += F("<h3>Nombre de canaux à afficher</h3>");
  page += F("<form action='/setDisplay' method='GET'>");
  page += F("<select name='count' onchange='this.form.submit()'>");
  page += F("<option value='128' ") + String(displayCount == 128 ? "selected" : "") + F(">128</option>");
  page += F("<option value='256' ") + String(displayCount == 256 ? "selected" : "") + F(">256</option>");
  page += F("<option value='384' ") + String(displayCount == 384 ? "selected" : "") + F(">384</option>");
  page += F("<option value='512' ") + String(displayCount == 512 ? "selected" : "") + F(">512</option>");
  page += F("</select>");
  page += F("</form>");

  // Boutons pour sauvegarder les presets
  page += F("<h3>Save Preset</h3>");
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"fetch('/save?p=" + String(i) + "')\">" + String(i) + "</button>";
  }

  // Boutons pour rappeler les presets
  page += F("<h3>Recall Preset</h3>");
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"fetch('/recall?p=" + String(i) + "').then(r=>location.reload());\">" + String(i) + "</button>";
  }

  // Slider pour ajuster la vitesse de mise à jour
  page += F("<h2>Vitesse de mise à jour</h2>");
  page += F("<input type='range' id='refreshRate' min='100' max='2000' step='100' value='1000' oninput='updateRate()'>");
  page += F("<p>Rafraîchissement : <span id='rateValue'>1000</span> ms</p>");

  // Tableau DMX
  page += F("<h2>Valeurs DMX</h2>");
  page += F("<table id='dmxTable'><tr>");
  page += F("<th></th>");
  for (int i = 1; i <= NUM_COLS; i++) {
    page += "<th>" + String(i) + "</th>";
  }
  page += F("</tr>");

  for (int row = 0; row < numRows; row++) {
    page += "<tr>";
    int firstChannel = (row * NUM_COLS) + 1;
    if (firstChannel <= displayCount) {
      page += "<td class='channel-index'>" + String(firstChannel) + "</td>";
    } else {
      page += "<td></td>";
    }
    for (int col = 0; col < NUM_COLS; col++) {
      int index = row * NUM_COLS + col;
      if (index < displayCount) {
        page += "<td id='ch" + String(index) + "' style='color:" ZERO_COLOR ";'>0</td>";
      } else {
        page += "<td></td>"; // en dehors des displayCount
      }
    }
    page += "</tr>";
  }
  page += F("</table>");

  // Script JavaScript pour les mises à jour AJAX
  page += F("<script>");
  page += F("let dmxValues = new Array(") + String(displayCount) + F(").fill(0);");
  page += F("let changeTimers = new Array(") + String(displayCount) + F(").fill(null);");
  page += F("let refreshRate = 1000;");
  page += F("let interval = setInterval(updateDMX, refreshRate);");

  page += F("function updateDMX() {");
  page += F("  fetch('/dmxdata')");
  page += F("    .then(response => response.json())");
  page += F("    .then(data => {");
  page += F("      for (let i = 0; i < ") + String(displayCount) + F("; i++) {");
  page += F("        let cell = document.getElementById('ch' + i);");
  page += F("        if (cell) {");
  page += F("          if (data[i] !== dmxValues[i]) {");
  page += F("            cell.style.backgroundColor = '" HIGHLIGHT_COLOR "';");
  page += F("            clearTimeout(changeTimers[i]);");
  page += F("            changeTimers[i] = setTimeout(() => { cell.style.backgroundColor = ''; }, 5000);");
  page += F("          }");
  page += F("          cell.innerText = data[i];");
  page += F("          cell.style.color = (data[i] === 0) ? '" ZERO_COLOR "' : 'black';");
  page += F("          dmxValues[i] = data[i];");
  page += F("        }");
  page += F("      }");
  page += F("    });");
  page += F("}");

  page += F("function updateRate() {");
  page += F("  refreshRate = document.getElementById('refreshRate').value;");
  page += F("  document.getElementById('rateValue').innerText = refreshRate;");
  page += F("  clearInterval(interval);");
  page += F("  interval = setInterval(updateDMX, refreshRate);");
  page += F("}");
  page += F("</script>");

  page += F("</body></html>");

  server.send(200, "text/html", page);
}

/***********************************************************************
 *                          SETUP & LOOP
 ***********************************************************************/
void setup()
{
  Serial.begin(115200);

  // Init EEPROM
  initEEPROM();  // EEPROM.begin(4096)

  // DMX
  dmx.init(512);

  // WiFi
  if (!wifiManager.autoConnect("Artnet-Receiver")) {
    wifiManager.startConfigPortal("Artnet-Receiver"); 
  }
  if (MDNS.begin("dmx")) {
    Serial.println("MDNS actif => http://dmx.local");
  }

  // Serveur Web
  server.on("/", handleRoot);
  server.on("/dmxdata", handleDMXData);
  server.on("/toggleMode", handleToggleMode);
  server.on("/save", handleSavePreset);
  server.on("/recall", handleRecallPreset);
  server.on("/setDisplay", handleSetDisplay); // Nouvelle route pour la sélection du nombre de canaux
  // Route pour reset WiFi
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);

  server.begin();

  // ArtNet
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);

  // Initialiser les valeurs DMX à zéro
  memset(dmxChannels,    0, sizeof(dmxChannels));
  memset(artnetChannels, 0, sizeof(artnetChannels));
}

void loop()
{
  MDNS.update();
  server.handleClient();
  artnet.read();

  // Mise à jour du tableau dmxChannels en fonction du mode
  if (isLive) {
    memcpy(dmxChannels, artnetChannels, MAX_CHANNELS);
  }

  // Envoi final sur la sortie DMX (toujours 512 canaux)
  for (int i = 0; i < MAX_CHANNELS; i++) {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

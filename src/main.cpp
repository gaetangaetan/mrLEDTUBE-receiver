/***********************************************************************
 * Exemple : Récepteur ArtNet -> DMX sur ESP8266 + gestion de 8 presets
 *           - Modes LIVE, PRESET et MANUAL
 *           - 8 presets enregistrables / rappelables (stockés en EEPROM)
 *           - Le bouton "Toggle Mode" passe entre les modes
 *           - Mode MANUAL avec tableau DMX interactif (clic & drag vertical) limité à 128 canaux
 *           - Limiter l'affichage à 128, 256, 384 ou 512 canaux
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
#define HIGHLIGHT_COLOR "#ffaae3"      // Couleur de surlignage pour valeurs modifiées

// --- Dimensions du tableau DMX ---
const int NUM_COLS = 20;  // 20 colonnes
int displayCount = 128;    // Nombre de canaux à afficher par défaut

#define MAX_CHANNELS 512 // 512 canaux DMX

// --- Nombre de presets ---
#define PRESET_COUNT 8

// --- EEPROM ---
#define EEPROM_SIZE 4096                // 8 presets × 512 octets = 4096

#define VERSION 217 // Incrémenté à chaque nouvelle version

/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

// Définir les modes disponibles
enum Mode { LIVE, PRESET, MANUAL };
Mode currentMode = LIVE; // Mode initialisé en LIVE

uint8_t dmxChannels[MAX_CHANNELS]; // Valeurs envoyées en DMX
uint8_t artnetChannels[MAX_CHANNELS]; // Valeurs reçues en ArtNet (utilisées en mode LIVE)

WiFiManager wifiManager;
ArtnetWifi artnet;
ESP8266WebServer server(80);

// Variables pour déterminer les blocs de canaux à afficher
int startChannel = 0; // Début des canaux affichés en mode MANUAL ou LIVE/PRESET

/***********************************************************************
 *                            FONCTIONS EEPROM
 ***********************************************************************/

/**
 * Initialise l'EEPROM en lecture/écriture
 */
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);  // Réserve 4096 octets en flash pour émuler l'EEPROM
  Serial.println("EEPROM initialisée.");
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
  Serial.println("Preset " + String(presetIndex + 1) + " sauvegardé en EEPROM.");
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
  Serial.println("Preset " + String(presetIndex + 1) + " rappelé depuis EEPROM.");
}

/***********************************************************************
 *                            FONCTIONS DMX
 ***********************************************************************/

// Callback Art-Net : reçoit un paquet DMX. On stocke dans artnetChannels si mode LIVE
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* packetData)
{
  if (currentMode == LIVE) {
    for (int i = 0; i < length && i < MAX_CHANNELS; i++) {
      artnetChannels[i] = packetData[i]; // Correction : Mise à jour directe sans startChannel
    }
    // Serial.println("Données ArtNet reçues en mode LIVE.");
  }
  // En mode PRESET et MANUAL, on ignore les données ArtNet
}

/***********************************************************************
 *                            ROUTES WEB
 ***********************************************************************/

/**
 * /dmxdata : retourne les valeurs dmxChannels en JSON
 * En mode MANUAL : 128 canaux du bloc sélectionné
 * Sinon : displayCount canaux à partir de startChannel
 */
void handleDMXData() {
  String json = "[";
  if (currentMode == MANUAL) {
    for (int i = 0; i < 128; i++) {
      json += String(dmxChannels[startChannel + i]);
      if (i < 127) json += ",";
    }
  }
  else {
    for (int i = 0; i < displayCount; i++) {
      if ((startChannel + i) < MAX_CHANNELS) {
        json += String(dmxChannels[startChannel + i]);
      } else {
        json += "0"; // Valeur par défaut si en dehors de la plage
      }
      if (i < (displayCount - 1)) json += ",";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
  //Serial.println("Données DMX envoyées via /dmxdata.");
}

/**
 * /toggleMode : passe de LIVE à PRESET à MANUAL ou inversement
 */
void handleToggleMode() {
  // Cycle through the modes
  switch (currentMode) {
    case LIVE:
      currentMode = PRESET;
      Serial.println("Mode changé en PRESET.");
      break;
    case PRESET:
      currentMode = MANUAL;
      Serial.println("Mode changé en MANUAL.");
      // Définir le startChannel basé sur displayCount
      switch(displayCount) {
        case 128: startChannel = 0; break;
        case 256: startChannel = 128; break;
        case 384: startChannel = 256; break;
        case 512: startChannel = 384; break;
        default: startChannel = 0; break;
      }
      break;
    case MANUAL:
      currentMode = LIVE;
      Serial.println("Mode changé en LIVE.");
      break;
  }
  
  // Envoyer une réponse JSON avec le nouveau mode
  String response = "{\"mode\":\"";
  response += (currentMode == LIVE) ? "LIVE" : (currentMode == PRESET) ? "PRESET" : "MANUAL";
  response += "\"}";
  server.send(200, "application/json", response);
}

/**
 * /save : enregistre les dmxChannels dans un preset, param 'p' = 1..8
 * Exemple : /save?p=3
 */
void handleSavePreset() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing preset number");
    Serial.println("Erreur : numéro de preset manquant lors de la sauvegarde.");
    return;
  }
  int presetIndex = server.arg("p").toInt() - 1; // 0..7
  if (presetIndex < 0 || presetIndex >= PRESET_COUNT) {
    server.send(400, "text/plain", "Invalid preset number");
    Serial.println("Erreur : numéro de preset invalide lors de la sauvegarde.");
    return;
  }
  // Ecrit dmxChannels (512 octets) en EEPROM
  savePresetToEEPROM(presetIndex, dmxChannels);
  server.send(200, "text/plain", "Preset " + String(presetIndex + 1) + " sauvegardé.");
}

/**
 * /recall : charge un preset dans dmxChannels, param 'p' = 1..8
 * Exemple : /recall?p=3
 */
void handleRecallPreset() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing preset number");
    Serial.println("Erreur : numéro de preset manquant lors du rappel.");
    return;
  }
  int presetIndex = server.arg("p").toInt() - 1;
  if (presetIndex < 0 || presetIndex >= PRESET_COUNT) {
    server.send(400, "text/plain", "Invalid preset number");
    Serial.println("Erreur : numéro de preset invalide lors du rappel.");
    return;
  }
  // Lit l'EEPROM et recopie dans dmxChannels
  loadPresetFromEEPROM(presetIndex, dmxChannels);
  server.send(200, "text/plain", "Preset " + String(presetIndex + 1) + " rappelé.");
}

/**
 * /resetwifi : efface les paramètres WiFi et redémarre l'ESP pour relancer le portail WiFiManager
 */
void handleResetWiFi() {
  server.send(200, "text/plain", "Réinitialisation WiFi... Redémarrage de l'ESP...");
  Serial.println("Réinitialisation des paramètres WiFi et redémarrage.");
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
    Serial.println("Erreur : paramètre count manquant lors de la définition de l'affichage.");
    return;
  }
  int count = server.arg("count").toInt();
  if (count != 128 && count != 256 && count != 384 && count != 512) {
    server.send(400, "text/plain", "Invalid count value");
    Serial.println("Erreur : valeur count invalide lors de la définition de l'affichage.");
    return;
  }
  
  displayCount = count;
  
  if (currentMode == MANUAL) {
    // Définir le startChannel basé sur count
    switch(count) {
      case 128: startChannel = 0; break;
      case 256: startChannel = 128; break;
      case 384: startChannel = 256; break;
      case 512: startChannel = 384; break;
      default: startChannel = 0; break;
    }
    Serial.println("Mode MANUAL : Affichage des canaux " + String(startChannel +1) + " à " + String(startChannel + 128) + ".");
  }
  else {
    // En mode LIVE ou PRESET, définir startChannel
    if (count <=256) {
      startChannel = 0;
    }
    else if (count <=384) {
      startChannel = 256;
    }
    else if (count <=512) {
      startChannel = 256;
    }
    Serial.println("Nombre de canaux à afficher défini sur " + String(count) + ".");
  }
  
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

/**
 * /setchannel : définit la valeur d'un canal DMX
 * param 'c' = canal (0..511)
 * param 'v' = valeur (0..255)
 * Exemple : /setchannel?c=10&v=255
 */
void handleSetChannel() {
  if (!server.hasArg("c") || !server.hasArg("v")) {
    server.send(400, "text/plain", "Missing channel or value");
    Serial.println("Erreur : canal ou valeur manquant lors de la mise à jour.");
    return;
  }
  int channel = server.arg("c").toInt();
  int value = server.arg("v").toInt();
  if (channel < 0 || channel >= MAX_CHANNELS || value < 0 || value > 255) {
    server.send(400, "text/plain", "Invalid channel or value");
    Serial.println("Erreur : canal ou valeur invalide lors de la mise à jour.");
    return;
  }
  dmxChannels[channel] = (uint8_t)value;
  //Serial.println("Canal " + String(channel + 1) + " défini à " + String(value) + ".");
  server.send(200, "text/plain", "Canal " + String(channel + 1) + " défini à " + String(value) + ".");
}

/***********************************************************************
 *                            PAGE PRINCIPALE
 ***********************************************************************/

void handleRoot() {
  String modeString;
  if (currentMode == LIVE) modeString = "LIVE";
  else if (currentMode == PRESET) modeString = "PRESET";
  else if (currentMode == MANUAL) modeString = "MANUAL";

  // Déterminer le nombre de canaux à afficher et le canal de départ
  int channelsToDisplay;
  int channelsStart;

  if (currentMode == MANUAL) {
    channelsToDisplay = 128;
    channelsStart = startChannel;
  }
  else {
    if (displayCount <=256) {
      channelsStart = 0;
      channelsToDisplay = displayCount;
    }
    else if (displayCount ==384) {
      channelsStart = 256;
      channelsToDisplay = 128;
    }
    else if (displayCount ==512) {
      channelsStart = 256;
      channelsToDisplay = 256;
    }
    else {
      channelsStart = 0;
      channelsToDisplay = 128; // Valeur par défaut
    }
  }

  String page = "<!DOCTYPE html><html><head><meta charset='utf-8'/>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"; // Ajout pour meilleure compatibilité mobile
  page += "<title>Interface DMX</title>";
  page += "<style>";
  page += "body { font-family: Arial, sans-serif; text-align: center; }";
  page += "table { border-collapse: collapse; margin: auto; width: 90%; max-width: 1200px; }";
  page += "td, th { border: 1px solid black; padding: 4px; width: 60px; text-align: center; transition: background 0.5s, color 0.5s; }";
  page += "th { background-color: lightgray; }";
  page += ".channel-index { font-weight: bold; background-color: #e0e0e0; }";
  page += ".btn { padding:6px 10px; margin:2px; cursor:pointer; }"; // style commun
  page += ".btn:hover { opacity: 0.8; }";
  
  // Styles pour les cellules interactives en MANUAL
  page += ".editable { cursor: ns-resize; position: relative; user-select: none; }";
  page += ".editable::after { content: ''; position: absolute; left: 50%; top: 50%; transform: translate(-50%, -50%); width: 10px; height: 10px; background: transparent; }";
  
  // Indicateur de modification
  page += ".highlight { background-color: " + String(HIGHLIGHT_COLOR) + "; }";

  page += "</style>";
  page += "</head><body>";

  page += "<h1>Interface ArtNet -> DMX + PRESETS</h1>";

  // Informations WiFi et Firmware
  page += "<p>Adresse IP de l'ESP8266 : " + WiFi.localIP().toString() + "</p>";
  page += "<p>Version firmware : " + String(VERSION) + "</p>";

  // Bouton pour reconfigurer le WiFi (relancer portail WiFiManager)
  page += "<form action='/resetwifi' method='POST'>";
  page += "<input type='submit' class='btn' value='Reconfigurer le WiFi' style='background:#f04; color:white;'/>";
  page += "</form>";

  // Indication du mode actuel + bouton de basculement
  page += "<h2>Mode actuel : <span id='mode'>" + modeString + "</span></h2>";
  page += "<button class='btn' onclick=\"toggleMode()\">Toggle Mode</button>";

  // Menu déroulant pour choisir le nombre de canaux
  page += "<h3>Nombre de canaux</h3>";
  page += "<form action='/setDisplay' method='GET'>";
  page += "<select name='count' onchange='this.form.submit()'>";
  // Ajouter les options avec la sélection appropriée
  if (currentMode != MANUAL) {
    page += "<option value='128' " + String(displayCount == 128 ? "selected" : "") + ">128</option>";
    page += "<option value='256' " + String(displayCount == 256 ? "selected" : "") + ">256</option>";
    page += "<option value='384' " + String(displayCount == 384 ? "selected" : "") + ">384</option>";
    page += "<option value='512' " + String(displayCount == 512 ? "selected" : "") + ">512</option>";
  }
  else {
    // En mode MANUAL, toujours 128 canaux affichés, mais startChannel basé sur selection
    page += "<option value='128' " + String((startChannel == 0) ? "selected" : "") + ">128</option>";
    page += "<option value='256' " + String((startChannel == 128) ? "selected" : "") + ">256</option>";
    page += "<option value='384' " + String((startChannel == 256) ? "selected" : "") + ">384</option>";
    page += "<option value='512' " + String((startChannel == 384) ? "selected" : "") + ">512</option>";
  }
  page += "</select>";
  page += "</form>";

  // Boutons pour sauvegarder les presets
  page += "<h3>Save Preset</h3>";
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"savePreset(" + String(i) + ")\">" + String(i) + "</button>";
  }

  // Boutons pour rappeler les presets
  page += "<h3>Recall Preset</h3>";
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"recallPreset(" + String(i) + ")\">" + String(i) + "</button>";
  }

  // Slider pour ajuster la vitesse de mise à jour
  page += "<h2>Vitesse de mise à jour</h2>";
  page += "<input type='range' id='refreshRate' min='100' max='2000' step='100' value='1000' oninput='updateRate()'>";
  page += "<p>Rafraîchissement : <span id='rateValue'>1000</span> ms</p>";

  // Tableau DMX pour tous les modes
  page += "<h2>Valeurs DMX</h2>";
  page += "<table id='dmxTable'><tr>";
  page += "<th></th>";
  for (int i = 1; i <= NUM_COLS; i++) {
    page += "<th>" + String(i) + "</th>";
  }
  page += "</tr>";

  // Générer les lignes du tableau en fonction des canaux à afficher
  for (int row = 0; row < (channelsToDisplay + NUM_COLS - 1) / NUM_COLS; row++) {
    page += "<tr>";
    int firstChannel = channelsStart + row * NUM_COLS + 1;
    if (firstChannel <= (channelsStart + channelsToDisplay)) {
      page += "<td class='channel-index'>" + String(firstChannel) + "</td>";
    }
    else {
      page += "<td></td>";
    }
    for (int col = 0; col < NUM_COLS; col++) {
      int index = channelsStart + row * NUM_COLS + col;
      if (index < (channelsStart + channelsToDisplay)) {
        // En mode MANUAL, toutes les 128 cellules sont éditables
        if (currentMode == MANUAL && index < (channelsStart + 128)) {
          page += "<td id='ch" + String(index) + "' class='editable' data-channel='" + String(index) + "' style='color:" + String(ZERO_COLOR) + ";'>" + String(dmxChannels[index]) + "</td>";
        }
        else {
          page += "<td id='ch" + String(index) + "' style='color:" + String(ZERO_COLOR) + ";'>" + String(dmxChannels[index]) + "</td>";
        }
      }
      else {
        page += "<td></td>"; // en dehors des canaux à afficher
      }
    }
    page += "</tr>";
  }
  page += "</table>";

  // Script JavaScript pour les mises à jour AJAX et la gestion des interactions
  page += "<script>\n";

  // Définir une variable JavaScript pour le mode actuel
  page += "let currentMode = '" + modeString + "';\n";

  // Fonction pour basculer le mode
  page += "function toggleMode() {\n";
  page += "  fetch('/toggleMode', { method: 'POST' })\n";
  page += "    .then(response => response.json())\n";
  page += "    .then(data => {\n";
  page += "      console.log('Mode toggled to:', data.mode);\n"; // Debug
  page += "      document.getElementById('mode').innerText = data.mode;\n";
  page += "      currentMode = data.mode;\n";
  page += "      // Recharger la page si on passe en mode MANUAL ou si on en sort\n";
  page += "      if (data.mode === 'MANUAL' || currentMode === 'MANUAL') {\n";
  page += "        window.location.reload();\n";
  page += "      }\n";
  page += "    })\n";
  page += "    .catch(error => console.error('Erreur:', error));\n";
  page += "}\n";

  // Fonction pour sauvegarder un preset
  page += "function savePreset(preset) {\n";
  page += "  fetch('/save?p=' + preset)\n";
  page += "    .then(response => {\n";
  page += "      if (response.ok) {\n";
  page += "        alert('Preset ' + preset + ' sauvegardé');\n";
  page += "      } else {\n";
  page += "        alert('Erreur lors de la sauvegarde du preset');\n";
  page += "      }\n";
  page += "    });\n";
  page += "}\n";

  // Fonction pour rappeler un preset
  page += "function recallPreset(preset) {\n";
  page += "  fetch('/recall?p=' + preset)\n";
  page += "    .then(response => {\n";
  page += "      if (response.ok) {\n";
  //page += "        alert('Preset ' + preset + ' rappelé');\n";
  page += "        window.location.reload();\n";
  page += "      } else {\n";
  page += "        alert('Erreur lors du rappel du preset');\n";
  page += "      }\n";
  page += "    });\n";
  page += "}\n";

  // Fonction pour mettre à jour la valeur du fader
  page += "function setChannelValue(channel, value) {\n";
  page += "  document.getElementById('value' + channel).innerText = value;\n";
  page += "  fetch('/setchannel?c=' + channel + '&v=' + value)\n";
  page += "    .then(response => {\n";
  page += "      if (!response.ok) {\n";
  page += "        alert('Erreur lors de la mise à jour du canal');\n";
  page += "      }\n";
  page += "    });\n";
  page += "}\n";

  // Script pour mettre à jour le tableau DMX en LIVE, PRESET et MANUAL
  page += "let dmxValues = new Array(" + String(channelsToDisplay) + ").fill(0);\n";
  page += "let changeTimers = new Array(" + String(channelsToDisplay) + ").fill(null);\n";

  page += "function updateDMX() {\n";
  page += "  if (currentMode === 'LIVE' || currentMode === 'PRESET') {\n";
  page += "    fetch('/dmxdata')\n";
  page += "      .then(response => response.json())\n";
  page += "      .then(data => {\n";
  page += "        for (let i = 0; i < " + String(channelsToDisplay) + "; i++) {\n";
  page += "          let channelIndex = " + String(channelsStart) + " + i;\n";
  page += "          let cell = document.getElementById('ch' + channelIndex);\n";
  page += "          if (cell) {\n";
  page += "            if (data[i] !== dmxValues[i]) {\n";
  page += "              cell.classList.add('highlight');\n";
  page += "              clearTimeout(changeTimers[i]);\n";
  page += "              changeTimers[i] = setTimeout(() => { cell.classList.remove('highlight'); }, 5000);\n";
  page += "            }\n";
  page += "            cell.innerText = data[i];\n";
  page += "            cell.style.color = (data[i] === 0) ? '" + String(ZERO_COLOR) + "' : 'black';\n";
  page += "            dmxValues[i] = data[i];\n";
  page += "          }\n";
  page += "        }\n";
  page += "      });\n";
  page += "  }\n";
  page += "}\n";

  // Fonction pour ajuster la vitesse de rafraîchissement
  page += "function updateRate() {\n";
  page += "  refreshRate = document.getElementById('refreshRate').value;\n";
  page += "  document.getElementById('rateValue').innerText = refreshRate;\n";
  page += "  clearInterval(interval);\n";
  page += "  interval = setInterval(updateDMX, refreshRate);\n";
  page += "}\n";

  // Initialiser l'intervalle de mise à jour
  page += "let refreshRate = 1000;\n";
  page += "let interval = setInterval(updateDMX, refreshRate);\n";

  // Fonction pour gérer les interactions en MANUAL
  page += "document.addEventListener('DOMContentLoaded', function() {\n";
  page += "  if (currentMode === 'MANUAL') {\n";
  page += "    let cells = document.querySelectorAll('.editable');\n";
  page += "    cells.forEach(cell => {\n";
  page += "      let channel = cell.getAttribute('data-channel');\n";
  page += "      let isDragging = false;\n";
  page += "      let startY = 0;\n";
  page += "      let startValue = 0;\n";
      
  // Gestion des événements de la souris
  page += "      // Gestion des événements de la souris\n";
  page += "      cell.addEventListener('mousedown', function(e) {\n";
  page += "        isDragging = true;\n";
  page += "        startY = e.clientY;\n";
  page += "        startValue = parseInt(cell.innerText);\n";
  page += "        e.preventDefault();\n";
  page += "      });\n";

  page += "      document.addEventListener('mousemove', function(e) {\n";
  page += "        if (isDragging) {\n";
  page += "          let deltaY = startY - e.clientY;\n";
  page += "          let deltaValue = Math.floor(deltaY / 1); // Ajuster la sensibilité\n"; // j'ai changé le /5 par /1 pour augmenter la sensibilité
  page += "          let newValue = startValue + deltaValue;\n";
  page += "          if (newValue < 0) newValue = 0;\n";
  page += "          if (newValue > 255) newValue = 255;\n";
  page += "          cell.innerText = newValue;\n";
  page += "          fetch('/setchannel?c=' + channel + '&v=' + newValue)\n";
  page += "            .then(response => {\n";
  page += "              if (!response.ok) {\n";
  page += "                alert('Erreur lors de la mise à jour du canal');\n";
  page += "              }\n";
  page += "            });\n";
  page += "        }\n";
  page += "      });\n";

  page += "      document.addEventListener('mouseup', function(e) {\n";
  page += "        if (isDragging) {\n";
  page += "          isDragging = false;\n";
  page += "        }\n";
  page += "      });\n";

  // Gestion des événements tactiles
  page += "      // Gestion des événements tactiles\n";
  page += "      cell.addEventListener('touchstart', function(e) {\n";
  page += "        if (e.touches.length === 1) { // Un seul doigt\n";
  page += "          isDragging = true;\n";
  page += "          startY = e.touches[0].clientY;\n";
  page += "          startValue = parseInt(cell.innerText);\n";
  page += "          e.preventDefault();\n";
  page += "        }\n";
  page += "      }, { passive: false });\n"; // passive: false pour pouvoir appeler preventDefault()

  page += "      document.addEventListener('touchmove', function(e) {\n";
  page += "        if (isDragging && e.touches.length === 1) {\n";
  page += "          let deltaY = startY - e.touches[0].clientY;\n";
  page += "          let deltaValue = Math.floor(deltaY / 1); // Ajuster la sensibilité\n";
  page += "          let newValue = startValue + deltaValue;\n";
  page += "          if (newValue < 0) newValue = 0;\n";
  page += "          if (newValue > 255) newValue = 255;\n";
  page += "          cell.innerText = newValue;\n";
  page += "          fetch('/setchannel?c=' + channel + '&v=' + newValue)\n";
  page += "            .then(response => {\n";
  page += "              if (!response.ok) {\n";
  page += "                alert('Erreur lors de la mise à jour du canal');\n";
  page += "              }\n";
  page += "            });\n";
  page += "          e.preventDefault();\n";
  page += "        }\n";
  page += "      }, { passive: false });\n";

  page += "      document.addEventListener('touchend', function(e) {\n";
  page += "        if (isDragging) {\n";
  page += "          isDragging = false;\n";
  page += "        }\n";
  page += "      });\n";

  page += "    });\n";
  page += "  }\n";
  page += "});\n";

  page += "</script>\n";

  page += "</body></html>";

  server.send(200, "text/html", page);
  Serial.println("Page principale servie.");
}

/***********************************************************************
 *                          SETUP & LOOP
 ***********************************************************************/
void setup()
{
  Serial.begin(115200);
  Serial.println("Démarrage du système DMX...");

  // Init EEPROM
  initEEPROM();

  // DMX
  dmx.init(512);
  Serial.println("DMX initialisé avec 512 canaux.");

  // WiFi
  if (!wifiManager.autoConnect("Artnet-Receiver")) {
    Serial.println("Échec de la connexion WiFi et portail de configuration démarré.");
    wifiManager.startConfigPortal("Artnet-Receiver"); 
  }
  Serial.println("Connecté au réseau WiFi.");
  if (MDNS.begin("dmx")) {
    Serial.println("MDNS actif => http://dmx.local");
  } else {
    Serial.println("Erreur lors de l'initialisation de MDNS.");
  }

  // Serveur Web
  server.on("/", handleRoot);
  server.on("/dmxdata", handleDMXData);
  server.on("/toggleMode", HTTP_POST, handleToggleMode); // Route définie avec POST
  server.on("/save", handleSavePreset);
  server.on("/recall", handleRecallPreset);
  server.on("/setDisplay", handleSetDisplay); // Route pour la sélection du nombre de canaux
  server.on("/setchannel", HTTP_GET, handleSetChannel); // Route pour définir un canal
  // Route pour reset WiFi
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);

  server.begin();
  Serial.println("Serveur Web démarré.");

  // ArtNet
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);
  Serial.println("Art-Net initialisé.");

  // Initialiser les valeurs DMX à zéro
  memset(dmxChannels,    0, sizeof(dmxChannels));
  memset(artnetChannels, 0, sizeof(artnetChannels));
  Serial.println("Valeurs DMX initialisées à zéro.");
}

void loop()
{
  MDNS.update();
  server.handleClient();
  artnet.read();

  // Mise à jour du tableau dmxChannels en fonction du mode
  if (currentMode == LIVE) {
    memcpy(dmxChannels, artnetChannels, MAX_CHANNELS);
  }
  // En mode PRESET, dmxChannels est déjà mis à jour via /recall
  // En mode MANUAL, dmxChannels est mis à jour via /setchannel

  // Envoi final sur la sortie DMX (512 canaux, sauf le canal 512)
  for (int i = 0; i < MAX_CHANNELS-1; i++) { // Correction pour éviter le canal 512
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

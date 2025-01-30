/***********************************************************************
 * Exemple : Récepteur ArtNet -> DMX sur ESP8266 + gestion de 8 presets
 *           - Modes LIVE, PRESET et MANUAL
 *           - 8 presets enregistrables / rappelables (stockés en EEPROM)
 *           - Le bouton "Toggle Mode" passe entre les modes
 *           - Mode MANUAL avec tableau DMX interactif (clic & drag vertical) limité aux 16 premiers canaux
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
int displayCount = 128;    // Nombre de canaux à afficher, réduit à 128 pour les tests

#define MAX_CHANNELS 512 // 512 canaux DMX

// --- Nombre de presets ---
#define PRESET_COUNT 8

// --- EEPROM ---
#define EEPROM_SIZE 4096                // 8 presets × 512 octets = 4096
// Optionnel : vous pourriez réserver un peu plus pour stocker une signature, etc.

#define VERSION 210 // Incrémenté à chaque nouvelle version

/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

// Définir les modes disponibles
enum Mode { LIVE, PRESET, MANUAL };
Mode currentMode = MANUAL; // Mode initialisé en MANUAL

int currentGroup = 0; // Groupe actuel en mode MANUAL

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
    for (int i = 0; i < length; i++) {
      if (i < MAX_CHANNELS) {
        artnetChannels[i] = packetData[i];
      }
    }
    Serial.println("Données ArtNet reçues en mode LIVE.");
  }
  // En mode PRESET et MANUAL, on ignore les données ArtNet
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
  Serial.println("Données DMX envoyées via /dmxdata.");
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
  Serial.println("Nombre de canaux à afficher défini sur " + String(displayCount) + ".");
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
  Serial.println("Canal " + String(channel + 1) + " défini à " + String(value) + ".");
  server.send(200, "text/plain", "Canal " + String(channel + 1) + " défini à " + String(value) + ".");
}

/**
 * /setDisplayGroup : définit le groupe de canaux à afficher en MANUAL
 * param 'group' = 0, 1, 2, ..., totalGroups-1
 * Exemple : /setDisplayGroup?group=1
 */
void handleSetDisplayGroup() {
  if (currentMode != MANUAL) {
    server.send(400, "text/plain", "Not in MANUAL mode");
    Serial.println("Erreur : tentative de définition du groupe en dehors du mode MANUAL.");
    return;
  }
  if (!server.hasArg("group")) {
    server.send(400, "text/plain", "Missing group parameter");
    Serial.println("Erreur : paramètre group manquant lors de la définition du groupe.");
    return;
  }
  int group = server.arg("group").toInt();
  int totalGroups = (displayCount + 15) / 16; // Nombre total de groupes de 16 canaux
  if (group < 0 || group >= totalGroups) {
    server.send(400, "text/plain", "Invalid group number");
    Serial.println("Erreur : numéro de groupe invalide lors de la définition du groupe.");
    return;
  }
  currentGroup = group;
  Serial.println("Groupe de canaux à afficher défini sur " + String(currentGroup + 1) + ".");
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

/***********************************************************************
 *                            PAGE PRINCIPALE
 ***********************************************************************/

void handleRoot() {
  String modeString;
  if (currentMode == LIVE) modeString = "LIVE";
  else if (currentMode == PRESET) modeString = "PRESET";
  else if (currentMode == MANUAL) modeString = "MANUAL";

  // Calculer le nombre de lignes en fonction de displayCount et NUM_COLS
  int numRows = (displayCount + NUM_COLS - 1) / NUM_COLS;

  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title>");
  page += F("<style>");
  page += F("body { font-family: Arial, sans-serif; text-align: center; }");
  page += F("table { border-collapse: collapse; margin: auto; width: 90%; max-width: 1200px; }");
  page += F("td, th { border: 1px solid black; padding: 4px; width: 60px; text-align: center; transition: background 0.5s, color 0.5s; }");
  page += F("th { background-color: lightgray; }");
  page += F(".channel-index { font-weight: bold; background-color: #e0e0e0; }");
  page += F(".btn { padding:6px 10px; margin:2px; cursor:pointer; }"); // style commun
  page += F(".btn:hover { opacity: 0.8; }");
  
  // Styles pour les cellules interactives en MANUAL
  page += F(".editable { cursor: ns-resize; position: relative; user-select: none; }");
  page += F(".editable::after { content: ''; position: absolute; left: 50%; top: 50%; transform: translate(-50%, -50%); width: 10px; height: 10px; background: transparent; }");
  
  // Indicateur de modification
  page += F(".highlight { background-color: ") + String(HIGHLIGHT_COLOR) + F("; }");

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
  page += F("<button class='btn' onclick=\"toggleMode()\">Toggle Mode</button>");

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
    page += "<button class='btn' onclick=\"savePreset(" + String(i) + ")\">" + String(i) + "</button>";
  }

  // Boutons pour rappeler les presets
  page += F("<h3>Recall Preset</h3>");
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"recallPreset(" + String(i) + ")\">" + String(i) + "</button>";
  }

  // Slider pour ajuster la vitesse de mise à jour
  page += F("<h2>Vitesse de mise à jour</h2>");
  page += F("<input type='range' id='refreshRate' min='100' max='2000' step='100' value='1000' oninput='updateRate()'>");
  page += F("<p>Rafraîchissement : <span id='rateValue'>1000</span> ms</p>");

  // Tableau DMX pour tous les modes
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
        // Ajouter une classe 'editable' si en mode MANUAL et si dans les 16 premiers canaux
        if (currentMode == MANUAL && index < 16) {
          page += "<td id='ch" + String(index) + "' class='editable' data-channel='" + String(index) + "' style='color:" ZERO_COLOR ";'>" + String(dmxChannels[index]) + "</td>";
        }
        else {
          page += "<td id='ch" + String(index) + "' style='color:" ZERO_COLOR ";'>" + String(dmxChannels[index]) + "</td>";
        }
      } else {
        page += "<td></td>"; // en dehors des displayCount
      }
    }
    page += "</tr>";
  }
  page += "</table>";

  // Script JavaScript pour les mises à jour AJAX et la gestion des interactions
  page += F("<script>\n");

  // Définir une variable JavaScript pour le mode actuel
  page += "let currentMode = '" + modeString + "';\n";

  // Fonction pour basculer le mode
  page += F("function toggleMode() {\n");
  page += F("  fetch('/toggleMode', { method: 'POST' })\n");
  page += F("    .then(response => response.json())\n");
  page += F("    .then(data => {\n");
  page += F("      console.log('Mode toggled to:', data.mode);\n"); // Debug
  page += F("      document.getElementById('mode').innerText = data.mode;\n");
  page += F("      currentMode = data.mode;\n");
  page += F("      // Recharger la page si on passe en mode MANUAL ou si on en sort\n");
  page += F("      if (data.mode === 'MANUAL' || currentMode === 'MANUAL') {\n");
  page += F("        window.location.reload();\n");
  page += F("      }\n");
  page += F("    })\n");
  page += F("    .catch(error => console.error('Erreur:', error));\n");
  page += F("}\n");

  // Fonction pour sauvegarder un preset
  page += F("function savePreset(preset) {\n");
  page += F("  fetch('/save?p=' + preset)\n");
  page += F("    .then(response => {\n");
  page += F("      if (response.ok) {\n");
  page += F("        alert('Preset ' + preset + ' sauvegardé');\n");
  page += F("      } else {\n");
  page += F("        alert('Erreur lors de la sauvegarde du preset');\n");
  page += F("      }\n");
  page += F("    });\n");
  page += F("}\n");

  // Fonction pour rappeler un preset
  page += F("function recallPreset(preset) {\n");
  page += F("  fetch('/recall?p=' + preset)\n");
  page += F("    .then(response => {\n");
  page += F("      if (response.ok) {\n");
  page += F("        alert('Preset ' + preset + ' rappelé');\n");
  page += F("        window.location.reload();\n");
  page += F("      } else {\n");
  page += F("        alert('Erreur lors du rappel du preset');\n");
  page += F("      }\n");
  page += F("    });\n");
  page += F("}\n");

  // Fonction pour changer le nombre de canaux affichés
  page += F("function changeGroup() {\n");
  page += F("  let group = document.getElementById('channelGroup').value;\n");
  page += F("  fetch('/setDisplayGroup?group=' + group)\n");
  page += F("    .then(response => {\n");
  page += F("      if (response.redirected) {\n");
  page += F("        window.location.href = response.url;\n");
  page += F("      }\n");
  page += F("    });\n");
  page += F("}\n");

  // Fonction pour mettre à jour la valeur du fader
  page += F("function setChannelValue(channel, value) {\n");
  page += F("  document.getElementById('value' + channel).innerText = value;\n");
  page += F("  fetch('/setchannel?c=' + channel + '&v=' + value)\n");
  page += F("    .then(response => {\n");
  page += F("      if (!response.ok) {\n");
  page += F("        alert('Erreur lors de la mise à jour du canal');\n");
  page += F("      }\n");
  page += F("    });\n");
  page += F("}\n");

  // Script pour mettre à jour le tableau DMX en LIVE, PRESET et MANUAL
  page += F("let dmxValues = new Array(") + String(displayCount) + F(").fill(0);\n");
  page += F("let changeTimers = new Array(") + String(displayCount) + F(").fill(null);\n");

  page += F("function updateDMX() {\n");
  page += F("  if (currentMode === 'LIVE' || currentMode === 'PRESET') {\n");
  page += F("    fetch('/dmxdata')\n");
  page += F("      .then(response => response.json())\n");
  page += F("      .then(data => {\n");
  page += F("        for (let i = 0; i < ") + String(displayCount) + F("; i++) {\n");
  page += F("          let cell = document.getElementById('ch' + i);\n");
  page += F("          if (cell) {\n");
  page += F("            if (data[i] !== dmxValues[i]) {\n");
  page += F("              cell.classList.add('highlight');\n");
  page += F("              clearTimeout(changeTimers[i]);\n");
  page += F("              changeTimers[i] = setTimeout(() => { cell.classList.remove('highlight'); }, 5000);\n");
  page += F("            }\n");
  page += F("            cell.innerText = data[i];\n");
  page += F("            cell.style.color = (data[i] === 0) ? '" ZERO_COLOR "' : 'black';\n");
  page += F("            dmxValues[i] = data[i];\n");
  page += F("          }\n");
  page += F("        }\n");
  page += F("      });\n");
  page += F("  }\n");
  page += F("}\n");

  // Fonction pour ajuster la vitesse de rafraîchissement
  page += F("function updateRate() {\n");
  page += F("  refreshRate = document.getElementById('refreshRate').value;\n");
  page += F("  document.getElementById('rateValue').innerText = refreshRate;\n");
  page += F("  clearInterval(interval);\n");
  page += F("  interval = setInterval(updateDMX, refreshRate);\n");
  page += F("}\n");

  // Initialiser l'intervalle de mise à jour
  page += F("let refreshRate = 1000;\n");
  page += F("let interval = setInterval(updateDMX, refreshRate);\n");

  // Fonction pour gérer les interactions en MANUAL
  page += F("document.addEventListener('DOMContentLoaded', function() {\n");
  page += F("  if (currentMode === 'MANUAL') {\n");
  page += F("    let cells = document.querySelectorAll('.editable');\n");
  page += F("    cells.forEach(cell => {\n");
  page += F("      let channel = cell.getAttribute('data-channel');\n");
  page += F("      let isDragging = false;\n");
  page += F("      let startY = 0;\n");
  page += F("      let startValue = 0;\n");
  
  page += F("      cell.addEventListener('mousedown', function(e) {\n");
  page += F("        isDragging = true;\n");
  page += F("        startY = e.clientY;\n");
  page += F("        startValue = parseInt(cell.innerText);\n");
  page += F("        e.preventDefault();\n");
  page += F("      });\n");
  
  page += F("      document.addEventListener('mousemove', function(e) {\n");
  page += F("        if (isDragging) {\n");
  page += F("          let deltaY = startY - e.clientY;\n");
  page += F("          let deltaValue = Math.floor(deltaY / 5); // Ajuster la sensibilité\n");
  page += F("          let newValue = startValue + deltaValue;\n");
  page += F("          if (newValue < 0) newValue = 0;\n");
  page += F("          if (newValue > 255) newValue = 255;\n");
  page += F("          cell.innerText = newValue;\n");
  page += F("          fetch('/setchannel?c=' + channel + '&v=' + newValue)\n");
  page += F("            .then(response => {\n");
  page += F("              if (!response.ok) {\n");
  page += F("                alert('Erreur lors de la mise à jour du canal');\n");
  page += F("              }\n");
  page += F("            });\n");
  page += F("        }\n");
  page += F("      });\n");
  
  page += F("      document.addEventListener('mouseup', function(e) {\n");
  page += F("        if (isDragging) {\n");
  page += F("          isDragging = false;\n");
  page += F("        }\n");
  page += F("      });\n");
  page += F("    });\n");
  page += F("  }\n");
  page += F("});\n");

  page += F("</script>\n");

  page += F("</body></html>");

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
  server.on("/setDisplayGroup", handleSetDisplayGroup); // Route pour définir le groupe en MANUAL
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

  // Envoi final sur la sortie DMX (toujours 512 canaux)
  for (int i = 0; i < MAX_CHANNELS; i++) {
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}

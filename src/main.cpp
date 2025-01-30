/***********************************************************************
 * Exemple : Récepteur ArtNet -> DMX sur ESP8266 
 *           + 8 presets (stockés en EEPROM)
 *           + 3 modes : LIVE / PRESET / MANUAL
 *           + DMX editable (click & drag) sur les 16 premiers canaux
 *           + Suspension du rafraîchissement pendant le drag
 ************************************************************************/

#include <Arduino.h>
#include <EEPROM.h>
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

#define VERSION 202

// --- Paramètres d'interface et de couleurs ---
#define ZERO_COLOR       "#f9f9f9"       // Couleur du texte pour valeurs DMX = 0
#define HIGHLIGHT_COLOR "#ffaae3"        // Couleur de surlignage pour valeurs modifiées

// --- Dimensions du tableau DMX ---
#define NUM_COLS     20   // 20 colonnes dans le tableau
#define NUM_ROWS     26   // 26 lignes (affichage des 512 canaux)
// On n'édite que les 16 premiers canaux en MANUAL (ch1..16).

#define MAX_CHANNELS 512  // 512 canaux DMX

// --- Nombre de presets ---
#define PRESET_COUNT 8

// --- EEPROM ---
#define EEPROM_SIZE 4096  // 8 presets × 512 octets = 4096

// --- Modes ---
#define MODE_LIVE   0
#define MODE_PRESET 1
#define MODE_MANUAL 2

/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

int currentMode = MODE_LIVE;           // Par défaut en mode LIVE
uint8_t dmxChannels[MAX_CHANNELS];     // Valeurs envoyées en DMX
uint8_t artnetChannels[MAX_CHANNELS];  // Valeurs reçues via ArtNet (mode LIVE)

WiFiManager wifiManager;
ArtnetWifi artnet;
ESP8266WebServer server(80);

/***********************************************************************
 *                            FONCTIONS EEPROM
 ***********************************************************************/

void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);  
}

void savePresetToEEPROM(int presetIndex, const uint8_t* data) {
  int offset = presetIndex * 512;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    EEPROM.write(offset + i, data[i]);
  }
  EEPROM.commit();
}

void loadPresetFromEEPROM(int presetIndex, uint8_t* data) {
  int offset = presetIndex * 512;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    data[i] = EEPROM.read(offset + i);
  }
}

/***********************************************************************
 *                            FONCTIONS DMX
 ***********************************************************************/

// Callback Art-Net : reçoit un paquet DMX si on est en mode LIVE
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* packetData)
{
  if (currentMode == MODE_LIVE) {
    for (int i = 0; i < length; i++) {
      if (i < MAX_CHANNELS) {
        artnetChannels[i] = packetData[i];
      }
    }
  }
}

/***********************************************************************
 *                            ROUTES WEB
 ***********************************************************************/

/**
 * /dmxdata : JSON avec les 512 valeurs DMX
 */
void handleDMXData() {
  String json = "[";
  for (int i = 0; i < MAX_CHANNELS; i++) {
    json += String(dmxChannels[i]);
    if (i < (MAX_CHANNELS - 1)) {
      json += ",";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

/**
 * /toggleMode : cycle entre LIVE -> PRESET -> MANUAL -> LIVE...
 */
void handleToggleMode() {
  currentMode = (currentMode + 1) % 3;
  server.send(200, "text/plain", "Mode toggled");
}

/**
 * /save : enregistre dmxChannels dans un preset => /save?p=1..8
 */
void handleSavePreset() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing preset number");
    return;
  }
  int presetIndex = server.arg("p").toInt() - 1;  // 0..7
  if (presetIndex < 0 || presetIndex >= PRESET_COUNT) {
    server.send(400, "text/plain", "Invalid preset number");
    return;
  }
  savePresetToEEPROM(presetIndex, dmxChannels);
  server.send(200, "text/plain", "Preset saved in slot " + String(presetIndex+1));
}

/**
 * /recall : charge un preset dans dmxChannels => /recall?p=1..8
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
  loadPresetFromEEPROM(presetIndex, dmxChannels);
  server.send(200, "text/plain", "Preset recalled: " + String(presetIndex+1));
}

/**
 * /resetwifi : efface les paramètres WiFi et redémarre
 */
void handleResetWiFi() {
  server.send(200, "text/plain", "Réinitialisation WiFi... Redémarrage de l'ESP...");
  delay(1000);
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * /setdmx : définit la valeur d'un canal => /setdmx?ch=X&val=Y (MODE_MANUAL)
 * Se limite aux canaux 1..16
 */
void handleSetDMX() {
  if (!server.hasArg("ch") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  int channel = server.arg("ch").toInt();  
  int value   = server.arg("val").toInt(); 
  // On limite à 1..16
  if (channel < 1 || channel > 16 || value < 0 || value > 255) {
    server.send(400, "text/plain", "Invalid parameters");
    return;
  }
  if (currentMode == MODE_MANUAL) {
    dmxChannels[channel - 1] = value;
  }
  server.send(200, "text/plain", "OK");
}

/***********************************************************************
 *                           PAGE PRINCIPALE
 ***********************************************************************/

void handleRoot() {
  // Déterminer le libellé du mode
  String modeStr;
  if (currentMode == MODE_LIVE)       modeStr = "LIVE";
  else if (currentMode == MODE_PRESET) modeStr = "PRESET";
  else if (currentMode == MODE_MANUAL) modeStr = "MANUAL";

  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>");
  page += F("<title>Interface DMX</title>");
  page += F("<style>");
  page += F("body { font-family: Arial, sans-serif; text-align: center; user-select: none; }");
  page += F("table { border-collapse: collapse; margin: auto; }");
  page += F("td, th { border: 1px solid black; padding: 4px; width: 30px; text-align: center;");
  page += F(" transition: background 0.5s, color 0.5s; }");
  page += F("th { background-color: lightgray; }");
  page += F(".channel-index { font-weight: bold; background-color: #e0e0e0; }");
  page += F(".btn { padding:6px 10px; margin:2px; cursor:pointer;}");
  page += F("</style>");
  page += F("</head><body>");
  
  page += F("<h1>Interface ArtNet -> DMX + PRESSETS + MANUAL</h1>");
  
  // Info WiFi + Firmware
  page += F("<p>Adresse IP de l'ESP8266 : ");
  page += WiFi.localIP().toString();
  page += F("</p>");
  page += F("<p>Version firmware : ");
  page += String(VERSION);
  page += F("</p>");

  // Bouton reconfig WiFi
  page += F("<form action='/resetwifi' method='POST'>");
  page += F("<input type='submit' class='btn' value='Reconfigurer le WiFi' style='background:#f04; color:white;'/>");
  page += F("</form>");

  // Mode + bouton Toggle
  page += F("<h2>Mode actuel : <span id='mode'>");
  page += modeStr;
  page += F("</span></h2>");
  page += F("<button class='btn' onclick=\"fetch('/toggleMode').then(r=>location.reload());\">Toggle Mode</button>");

  // Boutons SAVE
  page += F("<h3>Save Preset</h3>");
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"fetch('/save?p=" + String(i) + "')\">" + String(i) + "</button>";
  }

  // Boutons RECALL
  page += F("<h3>Recall Preset</h3>");
  for (int i = 1; i <= PRESET_COUNT; i++) {
    page += "<button class='btn' onclick=\"fetch('/recall?p=" + String(i) + "').then(r=>location.reload());\">" + String(i) + "</button>";
  }

  // Slider de rafraîchissement
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

  // Génération du tableau (512 canaux affichés)
  // Seuls canaux #1..16 seront "draggables" en MANUAL
  for (int row = 0; row < NUM_ROWS; row++) {
    page += "<tr>";
    int firstChannel = (row * NUM_COLS) + 1;
    if (firstChannel <= MAX_CHANNELS) {
      page += "<td class='channel-index'>" + String(firstChannel) + "</td>";
    } else {
      page += "<td></td>";
    }
    for (int col = 0; col < NUM_COLS; col++) {
      int index = row * NUM_COLS + col;
      if (index < MAX_CHANNELS) {
        // mode MANUAL + index<16 => onmousedown
        if (index < 16 && modeStr == "MANUAL") {
          page += "<td id='ch" + String(index) + "' ";
          page += "onmousedown='startDrag(this," + String(index+1) + ")' ";
          page += "style='color:" ZERO_COLOR ";'>0</td>";
        } else {
          // Lecture seule
          page += "<td id='ch" + String(index) + "' style='color:" ZERO_COLOR ";'>0</td>";
        }
      } else {
        page += "<td></td>";
      }
    }
    page += "</tr>";
  }
  page += F("</table>");

  // Script JS
  page += F("<script>");
  page += F("let dmxValues = new Array(512).fill(0);");
  page += F("let changeTimers = new Array(512).fill(null);");
  page += F("let refreshRate = 1000;");
  page += F("let interval = setInterval(updateDMX, refreshRate);");
  page += F("let isDragging = false;");

  // updateDMX => rafraîchit le tableau, sauf si isDragging
  page += F("function updateDMX() {");
  page += F("  if (isDragging) return; // On suspend le refresh pendant un drag");
  page += F("  fetch('/dmxdata')");
  page += F("    .then(response => response.json())");
  page += F("    .then(data => {");
  page += F("      for (let i = 0; i < 512; i++) {");
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

  // updateRate => modifie le tempo de rafraîchissement
  page += F("function updateRate() {");
  page += F("  refreshRate = document.getElementById('refreshRate').value;");
  page += F("  document.getElementById('rateValue').innerText = refreshRate;");
  page += F("  clearInterval(interval);");
  page += F("  interval = setInterval(updateDMX, refreshRate);");
  page += F("}");

  // Click & Drag
  page += F("let lastValue = 0;");
  page += F("let currentCell = null;");

  page += F("function startDrag(cell, channel) {");
  page += F("  isDragging = true;");
  page += F("  lastValue = parseInt(cell.innerText);");
  page += F("  currentCell = cell;");
  page += F("  document.addEventListener('mousemove', onMouseMove);");
  page += F("  document.addEventListener('mouseup', onMouseUp);");
  page += F("}");

  page += F("function onMouseMove(event) {");
  page += F("  if (isDragging && currentCell) {");
  page += F("    event.preventDefault();");
  page += F("    let newValue = lastValue + (event.movementY * -1);");
  page += F("    newValue = Math.min(255, Math.max(0, newValue));");
  page += F("    currentCell.innerText = newValue;");
  page += F("    currentCell.style.color = (newValue == 0) ? '" ZERO_COLOR "' : 'black';");
  page += F("    fetch('/setdmx?ch=' + channel + '&val=' + newValue);");
  page += F("    lastValue = newValue;");
  page += F("  }");
  page += F("}");

  page += F("function onMouseUp() {");
  page += F("  isDragging = false;");
  page += F("  currentCell = null;");
  page += F("  document.removeEventListener('mousemove', onMouseMove);");
  page += F("  document.removeEventListener('mouseup', onMouseUp);");
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
  initEEPROM();

  // DMX
  dmx.init(512);

  // WiFi
  if (!wifiManager.autoConnect("Artnet-Receiver")) {
    wifiManager.startConfigPortal("Artnet-Receiver"); 
  }
  if (MDNS.begin("dmx")) {
    Serial.println("MDNS actif => http://dmx.local");
  }

  // Routes
  server.on("/", handleRoot);
  server.on("/dmxdata", handleDMXData);
  server.on("/toggleMode", handleToggleMode);
  server.on("/save", handleSavePreset);
  server.on("/recall", handleRecallPreset);
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);
  server.on("/setdmx", handleSetDMX);

  server.begin();

  // ArtNet
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);

  // Valeurs initiales à 0
  memset(dmxChannels,    0, sizeof(dmxChannels));
  memset(artnetChannels, 0, sizeof(artnetChannels));
}

void loop()
{
  MDNS.update();
  server.handleClient();
  artnet.read();

  // Mise à jour dmxChannels selon le mode
  if (currentMode == MODE_LIVE) {
    // On recopie Artnet -> dmx
    memcpy(dmxChannels, artnetChannels, MAX_CHANNELS);
  }
  // En PRESET => dmxChannels est figé, modifié seulement par recall
  // En MANUAL => on modifie ch1..16 via /setdmx, le reste reste stable

  // Sortie DMX
  for (int i = 0; i < 511; i++) { // on ne copoie pas le canal 512 sinon il y a un bug (je ne sais pas pourquoi)
    dmx.write(i + 1, dmxChannels[i]);
  }
  dmx.update();
}
#define VERSION 21 // numéro de version pour m'y retrouver pendant le développement
#define VERSION_DATE "2025-08-12" // date de la version
#define BUTTON_PRESENT false
/*

Ce code fait partie d'un système de contrôle DMX sans fil pour ledstrip.

Le système est composé d'un émetteur qui dispose d'une entrée DMX (XLR 3 pins) et transmet les valeurs des 512 canaux à tous les récepteurs du système.

Le code qui suit sert à programmer les récepteurs.

Les contrôleurs utilisés sont 
- des ESP8266 (Wemos D1 mini) pour les récepteurs 
- et un ESP32 pour l'émetteur 

mais l'un comme l'autre fonctionnent sur les deux types de cartes, il faut juste adapter quelques lignes pour passer d'une carte à l'autre (essentiellement en ce qui concerne l'utilisation de ESP-NOW). 

Il n'y a pas de raison particulière à ce que l'émetteur utilise un ESP32 (je crois que c'est juste ce que j'avais sous la main quand je l'ai programmé)
L'émetteur comprend un bouton pour pouvoir régler le numéro de groupe (cf. modes de programmation) mais on peut très bien modifier quelques lignes pour que le numéro de groupe soit fixe

L'émetteur envoie 512 canaux DMX (par paquets de 128 canaux) et peut être décalé en modifiant la valeur de la variable "offsetDMXaddress" (qui vaut 0 par défaut)

Par exemple :
- si offsetDMXaddress vaut 0, la valeur à l'adresse 1 du récepteur DMX est envoyée à l'adresse 1 du récepteur
- si offsetDMXaddress vaut 240, la valeur à l'adresse 1 du récepteur DMX est envoyée à l'adresse 241 du récepteur

Si les adresses sont décalées, cela est totalement transparent du point de vue du récepteur. 
Quand on parle de la première adresse dans le code du récepteur, il s'agit de la première adresse des 512 valeurs qu'il reçoit, 
qu'importe le vrai canal DMX auquel cela correspond.

Quelques modes ont été programmés mais vu que j'utilise essentiellement le mode 6 (chaque groupe contrôlé sur trois canaux, en rgb), je n'ai pas passé un temps dingue là-dessus.
Pour plus de fonctionnalités, programmez vos propre modes dans la fonction DMX2LEDSTRIP

Fonctionnement :
Le canal DMX 1 précise le mode:
Mode = 0 : tous les pixels prennent la même valeur rgb représentée dans les canaux 2 3 4
Mode = 1 : rainbow | canal 2 = l'offset ajouté à chaque tube selon son groupe (0=tubes identiques) | canal 3 = rapprochement des couleurs | canal 4 = vitesse de défilement dans un sens ou dans l'autre (127=immobile)
Mode = 2 : idem avec un dimmer sur le canal 5 (le déplacement ne fonctionne plus -> à débugger)
Mode = 3 : random pixels | 3 canaux : couleur, nombre de pixels allumés, vitesse
Mode = 4 : idem avec fondu
Mode = 5 : idem par groupes de 1 à 5 pixels
Mode = 6 : rgb par grouope : chaque groupe utilise un 3 adresse pour une couleur RGB pour tous ses pixels (par exemple, le groupe 0 utilisera les adresses 2 3 4 pour ses valeurs RGB, le groupe 1 utilisera les adresses 5 6 7, ...)
// la numérotation des groupes commence à 0 (désolé, réflexe d'informaticien...)
Mode = 7 : "grésillement lumineux" : // 2 couleur unie ou par groupe 
// 3 longueur minimale 
// 4 longueur maximale 
// 5 durée d'une séquence de flickering 
// 6 durée d'une séquence éteinte 
// 7 durée d'un flicker individuel 
// 8 vitesse changement de pattern (pas encore utilisé ici) 
// 9 10 11 rgb grp0 | 12 13 14 rgb grp1 | etc.
Mode = 8, 9, 10, 11 : presets de flickering (pas de paramètres)
 
Mode = 8 à 254: tirets | 4 canaux : 1 longueur, 2 couleur, 3 intensité, 4 vitesse
Mode = 255 : affiche le numéro de groupe (des pixels espacés permettent de facilement voir le numéro du groupe, 7 pixels allumés= groupe 7)

remarque : le 04122024, j'ai réécrit tous les modes sauf les modes 0, 6 et 255 avec l'aide de chatgpt

SETUP (clic long pour y accéder ou en sortir) : réglage du numéro de groupe
Un certain nombre de LEDS correspondant au numéro de groupe clignotent en vert (si une seule led clignote en rouge, il s'agit du groupe 0 )
Le numéro de groupe est enregistré en EEPROM
*/


#define DATA_PIN D2        // pin de contrôle du strip led
#define BUTTONPIN D1       // on définit le pin positif du bouton (il s'agit d'un pullup, quand le bouton est relevé, la valeur du pin est HIGH, quand le bouton est enfoncé, le contact au GND est fait et la valeur est donc LOW)
#define BUTTONGROUNDPIN D5 // pour faciliter le montage, on utilise une pin pour fournir le GND au bouton

#define MAXLEDLENGTH 288  // longueur du strip led attention! version 2m de longueur!! remettre à 144 pour la version 1m
                          
                          // !!!  QUAND LE STRIP LED EST ALIMENTÉ PAR L'ESP (en cours de programmation, par exemple), NE PAS ALLUMER PLUS D'UNE DIZAINE DE LEDS !!!
                          
                          // en général, la longueur n'a pas particulièrement d'influence sur la latence du contrôleur mais ça peut être utile de la régler pour les programmes qui font le "tour" 
                          // du strip led (comme des segments de leds qui vont de bas en haut par exemple) 
                          // ou pour être sûr de ne pas demander plus de courant que ce que l'alimentation prévue ne peut fournir
                          

#define NBGROUPS 10 // nombre de groupes possibles (cf. modes de fonctionnement)

#define DEGRADE_FACTOR 0.5 // chatgpt - Proportion du segment utilisée pour le dégradé (0.5 = moitié du segment)
#define FADE_SPEED 0.02 // chatgpt -  Vitesse de transition : plus petit = transitions plus longues

// Signature pour valider les paquets Poulpylights
#define SIGNATURE_A 0x3C // 0011 1100
#define SIGNATURE_B 0x61 // 0110 0001  
#define SIGNATURE_C 0x05 // 0000 0101

// Codes de commande 
#define OTA_UPDATE_CMD 0xFA // Code de commande pour lancer une mise à jour firmware
#define REQUEST_MAC_CMD 0xFB // Code de commande pour demander l'adresse MAC
#define RESPONSE_MAC_CMD 0xFC // Code de commande pour envoyer l'adresse MAC
#define ENTER_REMOTE_SETUP_CMD 0xFD // Code de commande pour entrer en setup à distance
#define EXIT_REMOTE_SETUP_CMD 0xFE // Code de commande pour sortir du setup à distance
#define SET_GROUP_NUMBER_CMD 0xFF // Code de commande pour définir le numéro de groupe
#define PANIC_RESTART_CMD 0xF9 // Code de commande pour redémarrer tous les récepteurs

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>


// #include <DNSServer.h>
// #include <ESP8266WebServer.h>
// #include <WiFiManager.h> 
// WiFiManager wifiManager;
#define APNAME "mrLEDTUBE19"


#define EEPROM_SIZE 128  // Augmenté pour stocker les paramètres OTA (structure ~100 bytes)

// Paramètres OTA stockés directement en EEPROM (approche simple)
// Layout EEPROM à partir de l'adresse EEPROM_ADDR_OTA_PARAMS (12) :
// +0: pending (1 byte)
// +1: ssidLength (1 byte) 
// +2: passwordLength (1 byte)
// +3 à +34: SSID (32 bytes max)
// +35 à +97: Password (63 bytes max)

// Adresses EEPROM
#define EEPROM_ADDR_SETUP_ADDRESS   0
#define EEPROM_ADDR_SETUP_MODE      4  
#define EEPROM_ADDR_SETUP_TUBE      8
#define EEPROM_ADDR_OTA_PARAMS      12

#include <FastLED.h>

CRGB leds[MAXLEDLENGTH]; // objet représentant le ledstrip
CRGB flickerLeds[MAXLEDLENGTH]; // objet représentant la mémoire du ledstrip pendant l'effet flicker
uint8_t ledsTemp[MAXLEDLENGTH][3]; // tableau représentant les valeurs r g b de chaque led du ledstrip

#include "OneButton.h"
OneButton button1(BUTTONPIN, true); // Setup a new OneButton on pin BUTTONPIN.

//#include <ESP8266WiFiMulti.h>
#include <espnow.h> 
// library permettant d'utiliser le protocole de communication sans fil propriétaire d'Espressif (faire "comme du wifi" sans passer par toutes les couches du wifi)
// exemple : https://randomnerdtutorials.com/esp-now-esp32-arduino-ide/

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

uint8_t tempr =0;
uint8_t tempg =0;
uint8_t tempb =0;

unsigned long lastFlicker = 0;
unsigned long debutAllume= 0;
unsigned long lastUpdateFlickerPattern = 0;

uint8_t hueOffset =0;
uint8_t randomHueOffset =0;

#define RUNNING true
#define SETUP false

bool etat = RUNNING; // etat peut être en mode SETUP ou RUNNING : pour entrer ou sortir du SETUP, il faut appuyer longuement sur le bouton, le ledstrip affiche alors le numéro n du groupe en allumant n leds vertes

// ========== VARIABLES POUR LE SETUP À DISTANCE ==========
bool remoteSetupMode = false; // Mode setup à distance activé
unsigned long lastBlinkTime = 0; // Pour le clignotement du numéro de groupe
bool blinkState = false; // État actuel du clignotement
                     // on peut changer de groupe en appuyant  sur le bouton, on incrémente le numéro de groupe jusque la limite (fixée à 10 pour le moment) au delà de laquelle on recommence au groupe 0
                     // le groupe 0 conrrespond à 1 led rouge pour le distinguer du groupe 1 (une led verte)

int flashInterval;

uint8_t dmxChannels[512]; // tableau dans lequel seront stockées les valeurs des 512 canaux DMX
                          // les éléments 0 à 511 représentent les valeurs DMX de 1 à 512. Éternelle bataille des gens qui commencent la numérotation à 0 et ceux qui commencent à 1. Perso, je trouve qu'on devrait toujours commencer à 0 mais bon... ;)

typedef struct struct_dmx_packet // on divise les 512 adresses en 4 blocs de 128 adresses (on ne peut pas tout envoyer en une fois car la taille maximale des packets transmis par ESP-NOW est limitée à 250 bytes)
{                                
  uint8_t blockNumber;    // numéro du bloc (0..3)
  uint8_t dmxvalues[128]; // 128 valeurs DMX
  uint8_t data[100];      // données additionnelles (dont la signature en data[0..2])
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;

// ========== VARIABLES POUR L'ENVOI DE PAQUETS ESP-NOW ==========
// Adresse broadcast pour envoyer des réponses
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

// Variable pour stocker l'adresse MAC de l'émetteur (pour répondre directement)
uint8_t senderMacAddress[6];
bool hasSenderAddress = false;

// Variables pour le mode configuration à distance
bool remoteConfigActive = false;      // true si en mode configuration à distance
unsigned long lastConfigBlink = 0;   // Dernier clignotement en mode config
bool configBlinkState = false;       // État du clignotement (true = allumé)

void OnDataSent(u8 *mac_addr, u8 status) {} // quand on utilise ESP_NOW, la fonction OnDataSent doit être déclarée mais, concrètement, on n'en a pas besoin (pour l'instant, aucune donnée n'est renvoyée par les récepteurs à l'émetteur)
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len);

// Fonction pour vérifier si une commande est destinée à ce récepteur
bool isCommandForMe(const uint8_t* commandData) {
  // Récupérer notre adresse MAC
  uint8_t myMacAddress[6];
  WiFi.macAddress(myMacAddress);
  
  // Comparer avec l'adresse MAC dans commandData[4..9]
  return memcmp(&commandData[4], myMacAddress, 6) == 0;
}

// Fonction pour afficher le pattern de configuration à distance
void displayRemoteConfigPattern() {
  unsigned long currentTime = millis();
  
  // Clignotement toutes les 500ms
  if (currentTime - lastConfigBlink >= 500) {
    configBlinkState = !configBlinkState;
    lastConfigBlink = currentTime;
    
    FastLED.clear();
    
    if (configBlinkState) {
      // État allumé : pattern rouge/blanc alternant toutes les 10 LEDs
      // LEDs 0-9 blancs, 10-19 rouges, 20-29 blancs, 30-39 rouges, etc.
      
      for (int i = 0; i < MAXLEDLENGTH; i++) {
        int group = i / 10; // Groupe de 10 LEDs (0, 1, 2, 3...)
        if (group % 2 == 0) {
          leds[i] = CRGB::White; // Groupes pairs : blanc
        } else {
          leds[i] = CRGB::Red;   // Groupes impairs : rouge
        }
      }
    }
    // État éteint : rien à faire (FastLED.clear() déjà appelé)
    
    FastLED.show();
  }
}

// ========== FONCTIONNALITÉ OTA (MISE À JOUR FIRMWARE) ==========

// Fonctions utilitaires pour la gestion des paramètres OTA en EEPROM (approche simple)
void saveOTAParams(const char* ssid, const char* password) {
  uint8_t ssidLen = strlen(ssid);
  uint8_t passLen = strlen(password);
  
  // Sauvegarde directe octet par octet
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS, 1);      // pending = 1
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 1, ssidLen);
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 2, passLen);
  
  // Sauvegarde SSID
  for (int i = 0; i < ssidLen && i < 32; i++) {
    EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 3 + i, ssid[i]);
  }
  
  // Sauvegarde Password
  for (int i = 0; i < passLen && i < 63; i++) {
    EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 35 + i, password[i]);
  }
  
  EEPROM.commit();
  Serial.println("Paramètres OTA sauvegardés en EEPROM");
}

bool loadOTAParams(char* ssid, char* password) {
  //Serial.println("DEBUG: loadOTAParams() appelée");
  
  uint8_t pending = EEPROM.read(EEPROM_ADDR_OTA_PARAMS);
  //Serial.printf("DEBUG: pending = %d\n", pending);
  
  if (pending != 1) {
    //Serial.println("DEBUG: OTA pas en attente (pending != 1)");
    return false; // Pas d'OTA en attente
  }
  
  uint8_t ssidLen = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 1);
  uint8_t passLen = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 2);
  
  //Serial.printf("DEBUG: SSID length = %d, Password length = %d\n", ssidLen, passLen);
  
  // Lecture SSID
  for (int i = 0; i < ssidLen && i < 32; i++) {
    ssid[i] = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 3 + i);
  }
  ssid[ssidLen] = '\0';
  
  // Lecture Password
  for (int i = 0; i < passLen && i < 63; i++) {
    password[i] = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 35 + i);
  }
  password[passLen] = '\0';
  
  //Serial.printf("DEBUG: SSID lu = '%s'\n", ssid);
  
  return true;
}

void clearOTAParams() {
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS, 0);  // pending = 0
  EEPROM.commit();
  Serial.println("Paramètres OTA effacés de l'EEPROM");
}
// Fonction simplifiée pour programmer une mise à jour OTA
// Sauvegarde les paramètres en EEPROM et redémarre
// Paramètres extraits du paquet ESP-NOW selon le format :
// data[4] = longueur SSID, data[5...] = SSID, data[X] = longueur password, data[X+1...] = password
void handleOTAUpdate(uint8_t* otaData) {
  Serial.println("========== PROGRAMMATION MISE À JOUR OTA ==========");
  
  // Extraction des informations WiFi depuis le paquet
  uint8_t ssidLength = otaData[4];
  
  if (ssidLength == 0 || ssidLength > 32) {
    Serial.printf("ERREUR: Longueur SSID invalide (%d)\n", ssidLength);
    return;
  }
  
  // Extraction du SSID
  char ssid[33]; // 32 caractères max + null terminator
  memcpy(ssid, &otaData[5], ssidLength);
  ssid[ssidLength] = '\0';
  
  // Extraction de la longueur du password
  uint8_t passwordLength = otaData[5 + ssidLength];
  
  if (passwordLength > 63) { // WPA2 limite à 63 caractères
    Serial.printf("ERREUR: Longueur password invalide (%d)\n", passwordLength);
    return;
  }
  
  // Extraction du password
  char password[64]; // 63 caractères max + null terminator
  if (passwordLength > 0) {
    memcpy(password, &otaData[6 + ssidLength], passwordLength);
  }
  password[passwordLength] = '\0';
  
  Serial.printf("OTA programmée avec WiFi: %s\n", ssid);
  
  // Sauvegarde des paramètres et redémarrage
  saveOTAParams(ssid, password);
  Serial.println("Redémarrage pour mise à jour OTA...");
  
  // Délai plus long et flush du Serial avant redémarrage
  Serial.flush();
  delay(1000);
  
  // Redémarrage propre
  ESP.restart();
}

// Fonction pour exécuter la mise à jour OTA au démarrage
void executeOTAUpdate() {
  //Serial.println("DEBUG: executeOTAUpdate() appelée");
  
  char ssid[33];
  char password[64];
  
  if (!loadOTAParams(ssid, password)) {
    //Serial.println("DEBUG: Pas d'OTA en attente");
    return; // Pas d'OTA en attente
  }
  
  //Serial.println("DEBUG: OTA en attente trouvée !");
  
  Serial.println("========== EXÉCUTION MISE À JOUR OTA ==========");
  Serial.printf("Connexion au WiFi: %s\n", ssid);
  
  // IMPORTANT: Effacer les paramètres OTA AVANT le téléchargement
  // pour éviter la boucle infinie en cas de succès
  clearOTAParams();
  
  // URL fixe du firmware
  const char* firmwareURL = "http://mrledtubefirmware.gaetanstreel.com/firmware.bin";
  
  // Clignotement bleu pendant la connexion WiFi
  FastLED.clear();
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < min(10, MAXLEDLENGTH); j++) {
      leds[j] = CRGB::Blue;
    }
    FastLED.show();
    delay(200);
    FastLED.clear();
    FastLED.show();
    delay(200);
  }
  
  // Connexion au WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 30) { // 30 secondes maximum
    delay(1000);
    wifiTimeout++;
    Serial.print(".");
    
    // Indication visuelle : une LED bleue qui progresse
    FastLED.clear();
    for (int j = 0; j < min(wifiTimeout, MAXLEDLENGTH); j++) {
      leds[j] = CRGB::Blue;
    }
    FastLED.show();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nERREUR: Connexion WiFi échouée");
    // Indication visuelle : clignotement rouge
    for (int i = 0; i < 10; i++) {
      FastLED.clear();
      for (int j = 0; j < min(5, MAXLEDLENGTH); j++) {
        leds[j] = CRGB::Red;
      }
      FastLED.show();
      delay(100);
      FastLED.clear();
      FastLED.show();
      delay(100);
    }
    
    // Redémarrage en mode normal (paramètres déjà effacés)
    Serial.println("Redémarrage en mode normal...");
    delay(1000);
    ESP.restart();
    return;
  }
  
  Serial.println("\nWiFi connecté !");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());
  
  // Indication visuelle : vert fixe pendant le téléchargement
  FastLED.clear();
  for (int j = 0; j < MAXLEDLENGTH; j++) {
    leds[j] = CRGB::Green;
  }
  FastLED.show();
  
  // Mise à jour OTA
  Serial.print("Téléchargement du firmware depuis: ");
  Serial.println(firmwareURL);
  
  WiFiClient client;
  t_httpUpdate_return result = ESPhttpUpdate.update(client, firmwareURL);
  
  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("ERREUR: Mise à jour échouée (%d): %s\n", 
                    ESPhttpUpdate.getLastError(), 
                    ESPhttpUpdate.getLastErrorString().c_str());
      
      // Indication visuelle : clignotement rouge rapide
      for (int i = 0; i < 20; i++) {
        FastLED.clear();
        for (int j = 0; j < MAXLEDLENGTH; j++) {
          leds[j] = CRGB::Red;
        }
        FastLED.show();
        delay(50);
        FastLED.clear();
        FastLED.show();
        delay(50);
      }
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("Aucune mise à jour disponible");
      
      // Indication visuelle : orange fixe 2 secondes
      FastLED.clear();
      for (int j = 0; j < MAXLEDLENGTH; j++) {
        leds[j] = CRGB::Orange;
      }
      FastLED.show();
      delay(2000);
      break;
      
    case HTTP_UPDATE_OK:
      Serial.println("Mise à jour réussie ! Redémarrage...");
      
      // Indication visuelle : blanc fixe 1 seconde puis redémarrage
      FastLED.clear();
      for (int j = 0; j < MAXLEDLENGTH; j++) {
        leds[j] = CRGB::White;
      }
      FastLED.show();
      delay(1000);
      
      // Paramètres déjà effacés, redémarrage automatique par OTA
      ESP.restart();
      break;
  }
  
  // Si on arrive ici, c'est qu'il y a eu une erreur ou aucune mise à jour
  // Paramètres déjà effacés, redémarrage en mode normal
  Serial.println("Redémarrage en mode normal...");
  delay(1000);
  ESP.restart();
  
  Serial.println("========== FIN MISE À JOUR OTA ==========");
}
// ========== FIN FONCTIONNALITÉ OTA ==========

// ========== FONCTIONNALITÉ DÉCOUVERTE MAC ==========
// Fonction encapsulée pour envoyer l'adresse MAC de ce récepteur
// En réponse à une demande REQUEST_MAC_CMD
void sendMACResponse() {
  Serial.println("========== ENVOI ADRESSE MAC ==========");
  
  // Préparation du paquet de réponse
  struct_dmx_packet responsePacket;
  
  // Numéro de bloc = 255 pour indiquer que c'est un paquet de commande (pas de données DMX)
  responsePacket.blockNumber = 255;
  
  // Effacer les données DMX (non utilisées pour ce type de paquet)
  memset(responsePacket.dmxvalues, 0, sizeof(responsePacket.dmxvalues));
  
  // Effacer les données additionnelles
  memset(responsePacket.data, 0, sizeof(responsePacket.data));
  
  // Signature pour valider le paquet
  responsePacket.data[0] = SIGNATURE_A;
  responsePacket.data[1] = SIGNATURE_B;
  responsePacket.data[2] = SIGNATURE_C;
  
  // Code de commande de réponse MAC
  responsePacket.data[3] = RESPONSE_MAC_CMD;
  
  // Récupération de l'adresse MAC de ce ESP8266
  uint8_t macAddress[6];
  WiFi.macAddress(macAddress);
  
  // Insertion de l'adresse MAC dans le paquet (6 bytes)
  for (int i = 0; i < 6; i++) {
    responsePacket.data[4 + i] = macAddress[i];
  }
  
  // Informations supplémentaires utiles pour l'émetteur
  responsePacket.data[10] = setupTubeNumber; // Numéro de groupe actuel
  responsePacket.data[11] = VERSION;         // Version du firmware
  
  // Affichage de l'adresse MAC pour debug
  Serial.print("Adresse MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", macAddress[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.printf(" | Groupe: %d | Version: %d\n", setupTubeNumber, VERSION);
  
  // Envoyer directement à l'émetteur si on a son adresse MAC
  uint8_t* targetAddress;
  if (hasSenderAddress) {
    // Vérifier d'abord si le peer existe déjà (évite l'erreur -8)
    if (!esp_now_is_peer_exist(senderMacAddress)) {
      // Ajouter l'émetteur comme peer pour pouvoir lui répondre directement
      int peerResult = esp_now_add_peer(senderMacAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
      if (peerResult != 0) {
        Serial.printf("Erreur ajout peer émetteur: %d\n", peerResult);
        // En cas d'erreur, fallback sur broadcast
        targetAddress = broadcastAddress;
        Serial.println("Fallback: envoi en broadcast");
      } else {
        targetAddress = senderMacAddress;
        Serial.println("Peer ajouté, envoi direct à l'émetteur");
      }
    } else {
      targetAddress = senderMacAddress;
      Serial.println("Peer existe déjà, envoi direct à l'émetteur");
    }
  } else {
    targetAddress = broadcastAddress;
    Serial.println("Envoi en broadcast");
  }
  
  // Envoi du paquet AVANT l'indication visuelle
  uint8_t result = esp_now_send(targetAddress, (uint8_t *) &responsePacket, sizeof(responsePacket));
  
  if (result == 0) {
    Serial.println("Adresse MAC envoyée avec succès");
    // Note: Indication visuelle supprimée pour éviter les conflits FastLED/ESP-NOW
  } else {
    Serial.printf("Erreur envoi MAC: %d\n", result);
  }
  
  Serial.println("========== FIN ENVOI ADRESSE MAC ==========");
}
// ========== FIN FONCTIONNALITÉ DÉCOUVERTE MAC ==========

// ========== FONCTIONNALITÉ SETUP À DISTANCE ==========

// Fonction pour entrer en mode setup à distance
void enterRemoteSetup() {
  Serial.println("========== ENTRÉE SETUP À DISTANCE ==========");
  //Serial.println("DEBUG: Début enterRemoteSetup");
  
  // Test minimal - juste les variables essentielles
  lastConfigBlink = millis();
  configBlinkState = false;
  
  //Serial.println("DEBUG: Variables initialisées");
  Serial.printf("Mode setup à distance activé | Groupe actuel: %d\n", setupTubeNumber);
  //Serial.println("DEBUG: Fin enterRemoteSetup");
}

// Fonction pour sortir du mode setup à distance
void exitRemoteSetup() {
  Serial.println("========== SORTIE SETUP À DISTANCE ==========");
  //Serial.println("DEBUG: Début exitRemoteSetup");
  
  // Sauvegarde du numéro de groupe en EEPROM
  //Serial.println("DEBUG: Avant EEPROM.write");
  EEPROM.write(EEPROM_ADDR_SETUP_TUBE, setupTubeNumber);
  //Serial.println("DEBUG: Avant EEPROM.commit");
  bool success = EEPROM.commit();
  //Serial.println("DEBUG: Après EEPROM.commit");
  
  Serial.printf("Sauvegarde groupe %d en EEPROM: %s\n", 
                setupTubeNumber, 
                success ? "OK" : "ERREUR");
  
  // Note: remoteConfigActive est déjà désactivé dans OnDataRecv avant l'appel de cette fonction
  //Serial.println("DEBUG: Fin exitRemoteSetup");
  Serial.println("========== FIN SORTIE SETUP À DISTANCE ==========");
}

// Fonction pour définir un nouveau numéro de groupe
void setGroupNumber(uint8_t newGroupNumber) {
  if (newGroupNumber <= 31) { // Limite sécuritaire
    setupTubeNumber = newGroupNumber;
    Serial.printf("Nouveau groupe défini: %d\n", setupTubeNumber);
    
    // Reset du clignotement pour affichage immédiat
    lastBlinkTime = millis();
    blinkState = false;
  } else {
    Serial.printf("ERREUR: Numéro de groupe invalide: %d (max 31)\n", newGroupNumber);
  }
}

// Fonction principale pour gérer l'affichage en mode setup à distance
void handleRemoteSetup() {
  // Gestion du clignotement (toutes les 200ms)
  if (millis() - lastBlinkTime >= 200) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
  }
  
  // Effacer tout le strip
  FastLED.clear();
  
  // LEDs 100+ en blanc pour signaler la sélection
  for (int j = 100; j < MAXLEDLENGTH; j++) {
    leds[j] = CRGB::White;
  }
  
  // Affichage du numéro de groupe (seulement si clignotement actif)
  if (blinkState) {
    if (setupTubeNumber == 0) {
      // Groupe 0 : première LED en rouge
      if (0 < MAXLEDLENGTH) {
        leds[0] = CRGB::Red;
      }
    } else {
      // Groupe n : n LEDs vertes espacées de 10
      for (int j = 0; j < setupTubeNumber && (j * 10) < min(100, MAXLEDLENGTH); j++) {
        leds[j * 10] = CRGB::Green;
      }
    }
  }
  
  // Afficher les LEDs
  FastLED.show();
}

// ========== FIN FONCTIONNALITÉ SETUP À DISTANCE ==========

// Callback when data is received
// à chaque fois qu'un bloc de 128 valeurs est reçu par ESP_NOW, on met à jour ces valeurs dans le tableau dmxChannels
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  // Vérification de la taille d'un paquet DMX
  if (len == sizeof(struct_dmx_packet))
  {
    memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));

    // Vérifier la signature pour valider le paquet
    if (incomingDMXPacket.data[0] == SIGNATURE_A &&
        incomingDMXPacket.data[1] == SIGNATURE_B &&
        incomingDMXPacket.data[2] == SIGNATURE_C)
    {
      // ========== GESTION COMMANDES SPÉCIALES ==========
      
      // Vérifier si c'est un paquet de commande OTA
      if (incomingDMXPacket.data[3] == OTA_UPDATE_CMD)
      {
        Serial.println("Commande OTA détectée !");
        handleOTAUpdate(incomingDMXPacket.data);
        return; // Sortir immédiatement après traitement OTA
      }
      
      // Vérifier si c'est une demande d'adresse MAC
      if (incomingDMXPacket.data[3] == REQUEST_MAC_CMD)
      {
        Serial.println("Demande d'adresse MAC détectée !");
        
        // Stocker l'adresse MAC de l'émetteur pour pouvoir lui répondre directement
        memcpy(senderMacAddress, mac, 6);
        hasSenderAddress = true;
        
        sendMACResponse();
        return; // Sortir immédiatement après envoi de la réponse
      }
      
      // Vérifier si c'est une commande d'entrée en setup à distance
      if (incomingDMXPacket.data[3] == ENTER_REMOTE_SETUP_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        Serial.println("Commande ENTER_REMOTE_SETUP détectée pour moi !");
        remoteConfigActive = true;
        enterRemoteSetup();
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande de sortie du setup à distance
      if (incomingDMXPacket.data[3] == EXIT_REMOTE_SETUP_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        Serial.println("Commande EXIT_REMOTE_SETUP détectée pour moi !");
        remoteConfigActive = false;
        exitRemoteSetup();
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande de définition de groupe
      if (incomingDMXPacket.data[3] == SET_GROUP_NUMBER_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        uint8_t newGroup = incomingDMXPacket.data[10]; // Le groupe est maintenant en position 10
        Serial.printf("Commande SET_GROUP_NUMBER détectée : %d\n", newGroup);
        setGroupNumber(newGroup);
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande PANIC RESTART
      if (incomingDMXPacket.data[3] == PANIC_RESTART_CMD)
      {
        Serial.println("Commande PANIC_RESTART détectée ! Redémarrage immédiat...");
        delay(100); // Petit délai pour que le message s'affiche
        ESP.restart(); // Redémarrage immédiat
        return;
      }
      
      // ========== FIN GESTION COMMANDES SPÉCIALES ==========

      // IGNORER les paquets DMX normaux si en mode configuration à distance
      if (remoteConfigActive) {
        return; // Ne pas traiter les données DMX pendant la configuration
      }

      // Traitement normal des paquets DMX (si data[3] != OTA_UPDATE_CMD)
      uint8_t packetNumber = incomingDMXPacket.blockNumber;
      if (packetNumber < 4)
      {
        for (int i = 0; i < 128; i++)
        {
          dmxChannels[(packetNumber * 128) + i] = incomingDMXPacket.dmxvalues[i];
        }
      }
    }
  }
}

struct Color
{ // type utilisé par la fonction convertToColor
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

Color convertToColor(uint8_t index)
{ // fonction permettant de mapper les couleurs de manière continue sur 256 valeurs (pour pouvoir utilier un seul canal DMX pour représenter une couleur dans certains modes)
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



void flickering(uint8_t dmx1, uint8_t dmx2, uint8_t dmx3, uint8_t dmx4, uint8_t dmx5, uint8_t dmx6, uint8_t dmx7, uint8_t dmx8,uint8_t dmx9,uint8_t dmx10)
{
  // "grésillement lumineux"
// 2 couleur unie ou par groupe 
// 3 longueur minimale 
// 4 longueur maximale 
// 5 durée d'une séquence de flickering 
// 6 durée d'une séquence éteinte 
// 7 durée d'un flicker individuel 
// 8 vitesse changement de pattern (pas encore utilisé ici) 
// 9 10 11 rgb grp0 | 12 13 14 rgb grp1 | etc.
{
  // 1) Choix des canaux RGB selon "couleur unie (0)" ou "par groupe (1)"

  // 2) Calcul des longueurs mini/maxi pour la construction de segments
  int minimumSegmentLength = max(1, (dmx2* MAXLEDLENGTH) / 255);
  int maximumSegmentLength = max(1, (dmx3 * MAXLEDLENGTH) / 255);
  // Si jamais le max < min, on les inverse
  if (maximumSegmentLength < minimumSegmentLength) {
    int temp = maximumSegmentLength;
    maximumSegmentLength = minimumSegmentLength;
    minimumSegmentLength = temp;
  }

  // 3) Durée entre deux régénérations de pattern
  unsigned long dureeFlickerPattern = random(1, 100UL * dmx7);

  // => Régénération du pattern si on a dépassé cette durée
  if (millis() - lastUpdateFlickerPattern >= dureeFlickerPattern) {
    lastUpdateFlickerPattern = millis();

    // On construit un nouveau "pattern" de segments allumés/éteints dans flickerLeds[]
    bool allumeSegment = (random(2) == 1);  // random(2) => 0 ou 1
    for (int j = 0; j < MAXLEDLENGTH; ) {
      int segLen = random(minimumSegmentLength, maximumSegmentLength);
      // On s’assure de ne pas dépasser la fin du strip
      if (j + segLen > MAXLEDLENGTH) {
        segLen = MAXLEDLENGTH - j; 
      }

      // On remplit segLen pixels
      for (int k = 0; k < segLen; k++) {
        if (allumeSegment) {
          flickerLeds[j + k].r = dmx8;
          flickerLeds[j + k].g = dmx9;
          flickerLeds[j + k].b = dmx10;
        } else {
          flickerLeds[j + k] = CRGB::Black;
        }
      }

      // On alterne la phase (allumé/éteint)
      allumeSegment = !allumeSegment;

      // Avance de segLen
      j += segLen;
    }
  }

  // 4) Paramètres de la partie “flickering bloquant”
  unsigned long dureeAllume = random(5UL * dmx4, 10UL * dmx4);   // temps pendant lequel on va faire des flickers on/off
  unsigned long dureeEteint = random(5UL * dmx5, 10UL * dmx5);   // temps “éteint”
  unsigned long flickerMin  =  dmx6/10;   // min du flicker
  unsigned long flickerMax  =  dmx6;   // max du flicker

  // On “bloque” le programme pour ce temps-là
  debutAllume = millis();
  while (millis() - debutAllume <= dureeAllume) {
    // A chaque tour, on choisit une durée de flicker
    lastFlicker = millis();
    unsigned long dureeFlicker = random(flickerMin, flickerMax + 1);

    // On alterne allumé / éteint 
    static bool isOn = false;
    isOn = !isOn;

    if (isOn) {
      // On copie le pattern calculé dans flickerLeds[]
      for (int i = 0; i < MAXLEDLENGTH; i++) {
        leds[i] = flickerLeds[i];
      }
    } else {
      FastLED.clear();
    }
    FastLED.show();

    // On attend la fin du petit flicker
    while (millis() - lastFlicker < dureeFlicker) {
      delay(1);  
    }
  }

  // 5) Ensuite, on reste éteint un temps aléatoire dans [dureeEteint/2..dureeEteint]
  if (dureeEteint > 0) {
    delay(random(dureeEteint / 2, dureeEteint));
  }
  
}
}


// Contrôle du ledstrip proprement dit, en fonction des valeurs DMX
// !!! POUR AJOUTER UN MODE !!!
// Il suffit d'ajouter un bloc "CASE"
void DMX2LEDSTRIP()
{

  FastLED.clear();

  setupMode = dmxChannels[0]; // le canal 1 précise le mode des ledstrips

  double ledDimmer;
  int onLength;
  int offLength;
  double fxSpeed;
  double speedFactor;
  int ir = 3 * setupTubeNumber + 1;
  int ig = 3 * setupTubeNumber + 2;
  int ib = 3 * setupTubeNumber + 3;

  int segmentLength;

  Color rgbcolor;

// Offset utilisé par plusieurs modes
if (setupMode != 1) {
    hueOffset = 0;
} 


  switch (setupMode)
  {
  case 0: // Une seule couleur pour tous les pixels et tous les groupes. Les canaux 2 3 4 représentent les composantes R G B

    for (int j = 0; j < MAXLEDLENGTH; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  




case 1: // Mode Rainbow défilant avec contrôle du décalage, de la densité des couleurs (distance entre chaque couleur) et de la vitesse de défilement
{
    uint8_t colorDensity = dmxChannels[2]/2; // Canal 3 pour la densité des couleurs (distance entre pixels de même couleur)
    uint8_t rainbowSpeed = dmxChannels[3]; // Canal 4 pour la vitesse de déplacement
    
    

    // Calculer la densité des couleurs
    uint8_t deltaHue = map(colorDensity, 0, 255, 1, 50); // Plus colorDensity est élevé, plus les couleurs sont espacées

    // Générer un arc-en-ciel avec la densité contrôlée
    fill_rainbow(leds, MAXLEDLENGTH, (hueOffset + (setupTubeNumber * dmxChannels[1])) % 255, deltaHue);
    //fill_rainbow(leds, MAXLEDLENGTH, hueOffset+(setupTubeNumber*dmxChannels[1]), deltaHue);

    // Ajuster l'offset en fonction de la vitesse
    hueOffset += map(rainbowSpeed, 0, 255, -5, 5); // Contrôle de la vitesse et de la direction (-5 à 5)

    // Afficher les LEDs
    FastLED.show();
    break;
}

case 2: // Mode Rainbow avec intensité ajustable
{
    uint8_t colorDensity = dmxChannels[2] / 2; // Canal 3 pour la densité des couleurs (distance entre pixels de même couleur)
    uint8_t rainbowSpeed = dmxChannels[3];    // Canal 4 pour la vitesse de déplacement
    uint8_t intensity = dmxChannels[4];       // Canal 5 pour l’intensité générale des LEDs (0–255)

    // Calculer la densité des couleurs
    uint8_t deltaHue = map(colorDensity, 0, 255, 1, 50); // Plus colorDensity est élevé, plus les couleurs sont espacées

    // Générer un arc-en-ciel avec la densité contrôlée
    fill_rainbow(leds, MAXLEDLENGTH, (hueOffset + (setupTubeNumber * dmxChannels[1])) % 255, deltaHue);

    // Appliquer l’intensité générale
    for (int i = 0; i < MAXLEDLENGTH; i++) {
        leds[i].fadeLightBy(255 - intensity); // Réduire la luminosité en fonction de l'intensité
    }

    // Ajuster l'offset en fonction de la vitesse
    hueOffset += map(rainbowSpeed, 0, 255, -5, 5); // Contrôle de la vitesse et de la direction (-5 à 5)

    // Afficher les LEDs
    FastLED.show();
    break;
}


case 3: // Mode scintillement aléatoire
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numPixels = dmxChannels[2]/10;     // Nombre de pixels allumés simultanément
    uint8_t flickerRate = dmxChannels[3];   // Fréquence de scintillement (pixels par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static bool pixelStates[MAXLEDLENGTH] = {false}; // État de chaque pixel (allumé ou éteint)

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les pixels si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un pixel aléatoire (si nécessaire)
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (pixelStates[i] && random(0, 100) < 50) { // 50% de probabilité de s'éteindre
                pixelStates[i] = false;
            }
        }

        // Activer de nouveaux pixels aléatoires
        int activePixels = 0;
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (pixelStates[i]) {
                activePixels++;
            }
        }

        while (activePixels < numPixels) {
            int randomPixel = random(0, MAXLEDLENGTH);
            if (!pixelStates[randomPixel]) {
                pixelStates[randomPixel] = true;
                activePixels++;
            }
        }
    }

    // Appliquer les couleurs
    for (int i = 0; i < MAXLEDLENGTH; i++) {
        if (pixelStates[i]) {
            leds[i].r = pixelColor.r;
            leds[i].g = pixelColor.g;
            leds[i].b = pixelColor.b;
        } else {
            leds[i].r = 0;
            leds[i].g = 0;
            leds[i].b = 0;
        }
    }

    // Afficher les LEDs
    FastLED.show();
    break;
}



case 4: // Mode scintillement progressif
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numPixels = dmxChannels[2] / 10; // Nombre de pixels allumés simultanément (ajusté)
    uint8_t flickerRate = dmxChannels[3]/2;   // Fréquence de scintillement (pixels par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static float pixelIntensities[MAXLEDLENGTH] = {0.0}; // Intensité de chaque pixel (de 0.0 à 1.0)
    static int pixelTargets[MAXLEDLENGTH] = {0};         // État cible (1 = allumé, 0 = éteint)

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les pixels si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un pixel aléatoire (si nécessaire)
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (pixelTargets[i] == 1 && random(0, 100) < 50) { // 50% de probabilité de passer à l'état "éteint"
                pixelTargets[i] = 0;
            }
        }

        // Activer de nouveaux pixels aléatoires
        int activePixels = 0;
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (pixelTargets[i] == 1) {
                activePixels++;
            }
        }

        while (activePixels < numPixels) {
            int randomPixel = random(0, MAXLEDLENGTH);
            if (pixelTargets[randomPixel] == 0) {
                pixelTargets[randomPixel] = 1; // Passer à l'état "allumé"
                activePixels++;
            }
        }
    }

  for (int i = 0; i < MAXLEDLENGTH; i++) {
    if (pixelTargets[i] == 1) {
        pixelIntensities[i] = min(pixelIntensities[i] + FADE_SPEED, 1.0); // Augmenter progressivement
    } else {
        pixelIntensities[i] = max(pixelIntensities[i] - FADE_SPEED, 0.0); // Diminuer progressivement
    }
}


    // Appliquer les couleurs en fonction des intensités
    for (int i = 0; i < MAXLEDLENGTH; i++) {
        leds[i].r = pixelColor.r * pixelIntensities[i];
        leds[i].g = pixelColor.g * pixelIntensities[i];
        leds[i].b = pixelColor.b * pixelIntensities[i];
    }

    // Afficher les LEDs
    FastLED.show();
    break;
}


case 5: // Mode scintillement progressif par groupes
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numGroups = dmxChannels[2] / 10; // Nombre de groupes actifs simultanément (ajusté)
    uint8_t flickerRate = dmxChannels[3] / 2; // Fréquence de scintillement (groupes par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static float groupIntensities[MAXLEDLENGTH] = {0.0}; // Intensité de chaque groupe (de 0.0 à 1.0)
    static int groupTargets[MAXLEDLENGTH] = {0};         // État cible (1 = allumé, 0 = éteint)
    static int groupLengths[MAXLEDLENGTH] = {0};         // Longueur des groupes

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les groupes si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un groupe aléatoire (si nécessaire)
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (groupTargets[i] == 1 && random(0, 100) < 50) { // 50% de probabilité de passer à l'état "éteint"
                groupTargets[i] = 0;
            }
        }

        // Activer de nouveaux groupes aléatoires
        int activeGroups = 0;
        for (int i = 0; i < MAXLEDLENGTH; i++) {
            if (groupTargets[i] == 1) {
                activeGroups++;
            }
        }

        while (activeGroups < numGroups) {
            int randomGroupStart = random(0, MAXLEDLENGTH);
            int groupLength = random(1, 6); // Longueur aléatoire entre 1 et 5 pixels

            if (groupTargets[randomGroupStart] == 0) {
                groupTargets[randomGroupStart] = 1; // Passer à l'état "allumé"
                groupLengths[randomGroupStart] = groupLength; // Assigner une longueur aléatoire
                activeGroups++;
            }
        }
    }

    // Mettre à jour les intensités pour chaque groupe
    for (int i = 0; i < MAXLEDLENGTH; i++) {
        if (groupTargets[i] == 1) {
            groupIntensities[i] = min(groupIntensities[i] + FADE_SPEED, 1.0); // Augmenter progressivement
        } else {
            groupIntensities[i] = max(groupIntensities[i] - FADE_SPEED, 0.0); // Diminuer progressivement
        }

        // Appliquer les couleurs pour les pixels dans chaque groupe
        for (int j = 0; j < groupLengths[i]; j++) {
            int pixelIndex = (i + j) % MAXLEDLENGTH; // Gérer les débordements
            leds[pixelIndex].r = pixelColor.r * groupIntensities[i];
            leds[pixelIndex].g = pixelColor.g * groupIntensities[i];
            leds[pixelIndex].b = pixelColor.b * groupIntensities[i];
        }
    }

    // Afficher les LEDs
    FastLED.show();
    break;
}

   

  

  case 6: // Chaque groupe a une couleur unique pour tous ses pixels. Cette couleur est définie par trois canaux représentant ses valeurs RGB. 
         // Groupe 0 : RGB = 2 3 4, groupe 1 : RGB = 5 6 7, etc. 
    for (int j = 0; j < MAXLEDLENGTH * 3; j += 3)
    {
      leds[j / 3].r = dmxChannels[ir];
      leds[j / 3].g = dmxChannels[ig];
      leds[j / 3].b = dmxChannels[ib];
    }
    break;

case 7: // "grésillement lumineux" général

if(dmxChannels[1] !=0)
   {
    // Couleur : on prend le triplet (9,10,11) => groupe 0
    tempr = dmxChannels[8];
    tempg = dmxChannels[9];
    tempb = dmxChannels[10];
  } else {
    // Couleur dépendant du groupe => 9+3*g, 10+3*g, 11+3*g
    tempr = dmxChannels[8  + 3 * setupTubeNumber];
    tempg = dmxChannels[9  + 3 * setupTubeNumber];
    tempb = dmxChannels[10  + 3 * setupTubeNumber];
  }
flickering(dmxChannels[1],dmxChannels[2],dmxChannels[3],dmxChannels[4],dmxChannels[5],dmxChannels[6],dmxChannels[7],tempr,tempg,tempb);
break;

case 8: // "grésillement lumineux" preset 1
flickering(0,0,183,110,93,25,61,255,68,0);
break;

case 9: // "grésillement lumineux" preset 2
flickering(0,0,147,29,100,63,6,0,0,255);
break;

case 10: // "grésillement lumineux" preset 3
flickering(0,0,147,29,100,63,6,255,255,255);
break;

case 11: // "grésillement lumineux" preset 3
flickering(0,0,255,255,29,31,6,255,255,255);
break;


  case 255: // affichage du numéro de groupe
    for (int j = 0; j < setupTubeNumber; j++)
    {
      leds[10 * j].r = 255;
      leds[10 * j].g = 0;
      leds[10 * j].b = 0;
    }
    break;

 
  default: // Si aucun mode n'est explicitement défini pour la valeur dmxChannels[0] actuelle, on tombe sur le mode par défaut : segment de longueur, couleur, intensité, vitesse de déplacement contrôlés par les adresses 1 2 3 4
    
    segmentLength = setupMode; // Longueur du segment définie par le canal 1
    rgbcolor = convertToColor(dmxChannels[1]); // Couleur définie par le canal 2 

    ledDimmer = (double(dmxChannels[2]) / 255.0); // Intensité définie par le canal 3
    

    ir = rgbcolor.r * ledDimmer; // Rouge avec atténuation
    ig = rgbcolor.g * ledDimmer; // Vert avec atténuation
    ib = rgbcolor.b * ledDimmer; // Bleu avec atténuation

    onLength = segmentLength;    // Longueur des segments allumés
    offLength = segmentLength;   // Longueur des segments éteints

    // Calcul de la vitesse symétrique
    speedFactor = (double(dmxChannels[3]) - 127.5) / 127.5; // Plage de -1.0 à 1.0
    fxSpeed = speedFactor * 1.0; // Ajustement pour une vitesse maximale raisonnable

    // Calcul du décalage par groupe
    double groupOffset = setupTubeNumber * segmentLength / 2.0; // Décalage basé sur le numéro de groupe

    // Mise à jour continue de l'offset (ajout de fxSpeed pour chaque boucle)
    lastOffset += fxSpeed;
    if (lastOffset >= (onLength + offLength)) {
        lastOffset -= (onLength + offLength); // Limiter l'offset pour éviter de dépasser la plage
    } else if (lastOffset < 0) {
        lastOffset += (onLength + offLength); // Gérer les valeurs négatives
    }

    for (int j = 0; j < MAXLEDLENGTH; j++) {
        double fadeFactor = 1.0; // Par défaut, pas d'atténuation

        // Calcul de la position continue avec offset, incluant le décalage par groupe
        double positionWithOffset = fmod(j + lastOffset + groupOffset, onLength + offLength);

        // Vérifier si on est dans un segment allumé
        if (positionWithOffset < onLength) {
            double positionInSegment = positionWithOffset;

            // Appliquer le fondu (fade) au début et à la fin du segment
            if (positionInSegment < onLength * DEGRADE_FACTOR) {
                fadeFactor = positionInSegment / (onLength * DEGRADE_FACTOR); // Début progressif
            } else if (positionInSegment > onLength * (1.0 - DEGRADE_FACTOR)) {
                fadeFactor = (onLength - positionInSegment) / (onLength * DEGRADE_FACTOR); // Fin décroissante
            } else {
                fadeFactor = 1.0; // Pleine intensité au centre
            }

            // Appliquer la couleur avec l'atténuation calculée
            leds[j].r = ir * fadeFactor;
            leds[j].g = ig * fadeFactor;
            leds[j].b = ib * fadeFactor;
        } else {
            // LEDs dans le segment éteint
            leds[j].r = 0;
            leds[j].g = 0;
            leds[j].b = 0;
        }
    }
    break;

  }

  FastLED.show();
  delay(1);
}

// ----- button 1 callback functions
void click1() // en mode SETUP, chaque clic simple sur le bouton incrémente le numéro de groupe
{
  if(!BUTTON_PRESENT) return;
  if (etat == RUNNING)
    return; // en mode RUNNING, on ignore cette action
  setupTubeNumber = (setupTubeNumber + 1) % NBGROUPS;
}

void longPressStart1() // un clic long, permet de passer de RUNNING à SETUP et inversement (à la sortie du mode SETUP, on enregistre les données en mémoire persistante)
{
  if(!BUTTON_PRESENT) return;

  Serial.print("longpress | etat = ");
  Serial.println((etat ? "RUNNING" : "SETUP"));

  if (etat == SETUP) // avant de sortir du SETUP, on enregistre les données en mémoire persistante
  {
    EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS, setupAddress);
    EEPROM.write(EEPROM_ADDR_SETUP_MODE, setupMode);
    EEPROM.write(EEPROM_ADDR_SETUP_TUBE, setupTubeNumber);
    Serial.print("Commit =  ");
    Serial.println(EEPROM.commit());
  }

  etat = !etat;
}

void setup()
{
  randomHueOffset = rand()%256;
  Serial.begin(115200);
  Serial.println("");
  Serial.print("Version ");
  Serial.print(VERSION);
  Serial.print(" | ");
  Serial.println(VERSION_DATE);

  pinMode(BUTTONGROUNDPIN, OUTPUT); 
  digitalWrite(BUTTONGROUNDPIN, LOW); // on utilise BUTTONGROUNDPIN comme GND pour le bouton 1

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH); // Création d'un objet représentant le ledstrip pour FastLED // GRB ordering is typical

  // Initialiser EEPROM avant tout
  EEPROM.begin(EEPROM_SIZE);
  
  // Vérifier et exécuter une mise à jour OTA en attente AVANT d'initialiser ESP-NOW
  executeOTAUpdate();
  
  // Si on arrive ici, pas d'OTA en attente, continuer normalement
  WiFi.disconnect();
  ESP.eraseConfig(); // !!! COMMANDES IMPORTANTES !!! ESP_NOW peut parfois ne pas fonctionner si on n'exécute pas pas ces deux commandes. Je ne suis pas sûr que ce soit écrit dans la doc. C'est peut-être même un petit bug. Bref, il faut le savoir :)

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

  // ========== CONFIGURATION ESP-NOW POUR LES ENVOIS ==========
  // Définir le rôle ESP-NOW (nécessaire avant d'ajouter des peers)
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  
  // Ajouter l'adresse broadcast comme peer pour pouvoir envoyer des réponses
  // API ESP8266 (espnow.h) : esp_now_add_peer(mac, role, channel, key, key_len)
  int result = esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  if (result != 0) {
    Serial.printf("Erreur lors de l'ajout du peer broadcast: %d\n", result);
  } else {
    Serial.println("Adresse broadcast ajoutée comme peer ESP-NOW");
  }

  // link the button 1 functions.
  button1.setDebounceMs(50);
  button1.setPressMs(1000); 
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  // EEPROM déjà initialisé plus haut
  setupAddress = EEPROM.read(EEPROM_ADDR_SETUP_ADDRESS);
  setupMode = EEPROM.read(EEPROM_ADDR_SETUP_MODE);
  setupTubeNumber = EEPROM.read(EEPROM_ADDR_SETUP_TUBE);
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
  if(BUTTON_PRESENT) button1.tick(); // fonction vérifiant l'état du bouton

  // ========== PRIORITÉ AU SETUP À DISTANCE ==========
  if (remoteSetupMode) {
    handleRemoteSetup(); // Gestion du setup à distance
    delay(1); // Petit délai pour éviter la surcharge
    return; // Sortir immédiatement, ignorer le reste de la loop
  }
  // ========== FIN PRIORITÉ SETUP À DISTANCE ==========

  // ========== MODE CONFIGURATION À DISTANCE ==========
  if (remoteConfigActive) {
    displayRemoteConfigPattern(); // Affichage spécial clignotant
    delay(1); // Petit délai pour éviter la surcharge
    return; // Sortir immédiatement, ignorer le DMX
  }
  // ========== FIN MODE CONFIGURATION À DISTANCE ==========

  if (etat == RUNNING)
  {
    DMX2LEDSTRIP(); // on met à jour l'affichage du ledstrip
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube
  {
    FastLED.clear();
    if (setupTubeNumber == 0)
      leds[0].r = 250; // pour le groupe 0, on fait clignoter la première led en rouge
    else
    {
      for (int j = 1; j <= NBGROUPS * setupTubeNumber; j += NBGROUPS) // pour un groupe n (différent de 0), on fait clignotter n leds en vert
      {
        leds[j].r = 0;
        leds[j].g = 250;
        leds[j].b = 0;
      }
    }
    FastLED.show(); // les cinq lignes suivantes produisent un clignottement (200ms)
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }
}

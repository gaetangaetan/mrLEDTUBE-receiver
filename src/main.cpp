/*
Cette version servira à contrôler un gradateur à un seul canal.
Le contrôleur n'aura pas de bouton. Je vais donc ajouter un mécanisme pour pouvoir régler le canal : 
Quand les canaux DMX 1 et 2 vaudront 254 tous les deux, le contrôleur enregistrera en mémoire persistante la valeur représentée par la somme des canaux DMX 3 et 4.

VERSION GRADATEUR AC - SANS ZERO CROSSING
Cette version utilise un simple contrôle PWM pour piloter un module gradateur AC.
Pour l'instant, on ignore le pin ZC (zero crossing) et on utilise seulement le pin PWM.

Le système de réception DMX via ESP-NOW est conservé.
*/

#define PWM_PIN D2        // pin de contrôle PWM du gradateur AC
#define BUTTONPIN D1      // on définit le pin positif du bouton (il s'agit d'un pullup)
#define BUTTONGROUNDPIN D5 // pour faciliter le montage, on utilise une pin pour fournir le GND au bouton

// Paramètres du gradateur
#define PWM_FREQUENCY 1000  // Fréquence PWM en Hz (à ajuster selon le module)
#define PWM_RESOLUTION 10   // Résolution PWM (10 bits = 0-1023)
#define PWM_MAX_VALUE 1023  // Valeur PWM maximale

// Paramètres de configuration
#define EEPROM_ADDR_DMX_CHANNEL 16  // Adresse EEPROM pour stocker le canal DMX
#define DEFAULT_DMX_CHANNEL 1       // Canal DMX par défaut

#define SIGNATURE_A 0x3C 
#define SIGNATURE_B 0x61 
#define SIGNATURE_C 0x05 

#define SIGNATURE_NORMAL_MODE  0xA7 
#define SIGNATURE_EXTENDED_MODE 0x4D 

#define DATA_ADDRESS_SIGNATURE_A 0 
#define DATA_ADDRESS_SIGNATURE_B 1 
#define DATA_ADDRESS_SIGNATURE_C 2 
#define DATA_ADDRESS_EXTENDED_MODE 3 
#define EEPROM_ADDR_EXTENDED_MODE 12

bool extendedMode = false; // Toujours en mode normal
int dmxChannel = DEFAULT_DMX_CHANNEL;  // Canal DMX actuel

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 

WiFiManager wifiManager;
#define APNAME "mrLEDTUBE22"
#define VERSION 235 // Version gradateur AC

#define EEPROM_SIZE 32

#include "OneButton.h"
OneButton button1(BUTTONPIN, true);

#include <ESP8266WiFiMulti.h>
#include <espnow.h> 

#define RUNNING true
#define SETUP false

bool etat = RUNNING;
int flashInterval;
unsigned long lastDmxChannelCheck = 0;
bool dmxChannelConfigMode = false;

uint8_t dmxChannels[512]; // tableau des 512 canaux DMX

// Déclarations forward
void checkDMXChannelConfig();
void setDimmerPWM(uint8_t dmxValue);

// Structures pour ESP-NOW (conservées du code original)
typedef struct struct_dmx_packet 
{                                
  uint8_t blockNumber;    
  uint8_t dmxvalues[128]; 
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket; 

typedef struct struct_dmx_packet_ext {
  uint8_t blockNumber;
  uint8_t dmxvalues[128];
  uint8_t data[100];
} struct_dmx_packet_ext;

struct_dmx_packet_ext incomingDMXPacketExt;

void OnDataSent(u8 *mac_addr, u8 status) {} 

// Callback when data is received
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  if (extendedMode) {
    memcpy(&incomingDMXPacketExt, incomingData, sizeof(incomingDMXPacketExt));
    if (incomingDMXPacketExt.data[DATA_ADDRESS_SIGNATURE_A] != SIGNATURE_A ||
        incomingDMXPacketExt.data[DATA_ADDRESS_SIGNATURE_B] != SIGNATURE_B ||
        incomingDMXPacketExt.data[DATA_ADDRESS_SIGNATURE_C] != SIGNATURE_C) {
      return;
    }
    uint8_t packetNumber = incomingDMXPacketExt.blockNumber;
    for (int i = 0; i < 128; i++) {
      dmxChannels[(packetNumber * 128) + i] = incomingDMXPacketExt.dmxvalues[i];
    }
  } else {
    memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));
    uint8_t packetNumber = incomingDMXPacket.blockNumber;
    for (int i = 0; i < 128; i++)
    {
      dmxChannels[(packetNumber * 128) + i] = incomingDMXPacket.dmxvalues[i];
    }
  }
}

// Fonction pour contrôler le gradateur AC
void setDimmerPWM(uint8_t dmxValue) {
  // Convertir la valeur DMX (0-255) en valeur PWM (0-1023)
  int pwmValue = map(dmxValue, 0, 255, 0, PWM_MAX_VALUE);
  static int lastPwmValue = -1;
  // Écrire la valeur PWM
  analogWrite(PWM_PIN, pwmValue);
  // Afficher uniquement si la valeur change
  if (pwmValue != lastPwmValue) {
    Serial.print("DMX: ");
    Serial.print(dmxValue);
    Serial.print(" -> PWM: ");
    Serial.println(pwmValue);
    lastPwmValue = pwmValue;
  }
}

// Fonction pour vérifier la configuration du canal DMX
void checkDMXChannelConfig() {
  // Vérifier si les canaux 1 et 2 valent 254
  if (dmxChannels[0] == 254 && dmxChannels[1] == 254) {
    if (!dmxChannelConfigMode) {
      // Entrer en mode configuration
      dmxChannelConfigMode = true;
      lastDmxChannelCheck = millis();
      Serial.println("Mode configuration canal DMX activé");
    }
    
    // Calculer le nouveau canal à partir des canaux 3 et 4
    int newChannel = dmxChannels[2] + dmxChannels[3]; // Canaux 3 et 4
    
    // Valider le canal (1-512)
    if (newChannel >= 1 && newChannel <= 512) {
      // Vérifier si la valeur a réellement changé avant d'écrire en EEPROM
      if (newChannel != dmxChannel) {
        dmxChannel = newChannel;
        // Sauvegarder en EEPROM seulement si la valeur a changé
        EEPROM.write(EEPROM_ADDR_DMX_CHANNEL, dmxChannel & 0xFF);
        EEPROM.write(EEPROM_ADDR_DMX_CHANNEL + 1, (dmxChannel >> 8) & 0xFF);
        EEPROM.commit();
        Serial.print("Nouveau canal DMX configuré et sauvegardé: ");
        Serial.println(dmxChannel);
      }
    }
        } else {
    dmxChannelConfigMode = false;
  }
}

// Fonction principale de contrôle du gradateur
void DMX2DIMMER()
{
  // Vérifier si on doit configurer le canal DMX
  checkDMXChannelConfig();
  
  // Lire la valeur du canal DMX configuré
  uint8_t dimmerValue = dmxChannels[dmxChannel - 1]; // -1 car les indices commencent à 0
  
  // Appliquer la valeur au gradateur
  setDimmerPWM(dimmerValue);
}

// Fonctions bouton (simplifiées)
void click1() {
  if (etat == RUNNING) return;
  // En mode setup, on peut afficher le canal actuel
  Serial.print("Canal DMX actuel: ");
  Serial.println(dmxChannel);
}

void longPressStart1() {
  Serial.print("longpress | etat = ");
  Serial.println((etat ? "RUNNING" : "SETUP"));

  if (etat == SETUP) {
    // Vérifier si les paramètres ont changé avant de sauvegarder
    int currentSavedChannel = EEPROM.read(EEPROM_ADDR_DMX_CHANNEL) | (EEPROM.read(EEPROM_ADDR_DMX_CHANNEL + 1) << 8);
    if (currentSavedChannel != dmxChannel) {
      EEPROM.write(EEPROM_ADDR_DMX_CHANNEL, dmxChannel & 0xFF);
      EEPROM.write(EEPROM_ADDR_DMX_CHANNEL + 1, (dmxChannel >> 8) & 0xFF);
      EEPROM.commit();
      Serial.println("Paramètres sauvegardés (canal modifié)");
    } else {
      Serial.println("Paramètres inchangés (pas de sauvegarde)");
    }
  }

  etat = !etat;
}

// Fonctions OTA (conservées du code original)
bool otaInProgress = false;
const uint8_t otaSequence[12] = {4,4,4,7,1,9,4,4,4,7,1,9};

bool isOtaSequence() {
  for (int i = 0; i < 12; i++) {
    if (dmxChannels[20 + i] != otaSequence[i]) return false;
  }
  return true;
}

void checkForOtaUpdate() {
  static bool alreadyTriggered = false;
  if (isOtaSequence() && !otaInProgress && !alreadyTriggered) {
    otaInProgress = true;
    alreadyTriggered = true;
    Serial.println("Déclenchement de la mise à jour OTA...");
    
    // Éteindre le gradateur pendant la mise à jour
    analogWrite(PWM_PIN, 0);
    
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin("mrVOOlpy", "youhououhou");
    
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      Serial.print(".");
      delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connecté, lancement de la mise à jour...");
        WiFiClient client;
        t_httpUpdate_return ret = ESPhttpUpdate.update(client, "http://mrledtubefirmware.gaetanstreel.com/firmware.bin");
      
        if (ret == HTTP_UPDATE_OK) {
          Serial.println("Mise à jour réussie, redémarrage...");
          ESP.restart();
        } else {
          Serial.printf("Erreur OTA (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        otaInProgress = false;
      }
    } else {
      Serial.println("Échec connexion WiFi pour OTA");
      otaInProgress = false;
    }
  }
  
  if (!isOtaSequence()) {
    alreadyTriggered = false;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("");
  Serial.print("Version Gradateur AC ");
  Serial.println(VERSION);

  // Configuration des pins
  pinMode(BUTTONGROUNDPIN, OUTPUT); 
  digitalWrite(BUTTONGROUNDPIN, LOW);
  pinMode(PWM_PIN, OUTPUT);
  
  // Initialiser le PWM
  analogWriteFreq(PWM_FREQUENCY);
  analogWriteRange(PWM_MAX_VALUE);
  analogWrite(PWM_PIN, 0); // Démarrer éteint

  // Configuration WiFi et ESP-NOW
  WiFi.disconnect();
  ESP.eraseConfig();
  WiFi.mode(WIFI_STA);
  
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("\nESP-Now Receiver - Gradateur AC");

  if (esp_now_init() != 0) {
    Serial.println("Problem during ESP-NOW init");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Configuration du bouton
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  // Configuration EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Lire le canal DMX sauvegardé
  int savedChannel = EEPROM.read(EEPROM_ADDR_DMX_CHANNEL) | (EEPROM.read(EEPROM_ADDR_DMX_CHANNEL + 1) << 8);
  if (savedChannel >= 1 && savedChannel <= 512) {
    dmxChannel = savedChannel;
  }

  Serial.print("Paramètres récupérés : Canal DMX = ");
  Serial.print(dmxChannel);
  Serial.print(" | Mode étendu = ");
  Serial.println(extendedMode ? "ON" : "OFF");

  // Test initial du gradateur
  Serial.println("Test gradateur...");
  for (int i = 0; i <= 255; i += 51) {
    setDimmerPWM(i);
    delay(500);
  }
  setDimmerPWM(0); // Éteindre
  
  Serial.println("Gradateur AC prêt !");
}

void loop() 
{
  //button1.tick();
  //checkForOtaUpdate();

  if (etat == RUNNING && !otaInProgress) {
    DMX2DIMMER(); // Contrôler le gradateur
  } else if (!otaInProgress) {
    // Mode SETUP - clignoter pour indiquer le canal
    Serial.print("Mode SETUP - Canal DMX: ");
    Serial.println(dmxChannel);
    delay(1000);
  }
  
  delay(10); // Petit délai pour éviter la surcharge
}



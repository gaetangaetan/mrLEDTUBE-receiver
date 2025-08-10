#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Adafruit_NeoPixel.h>
extern "C" {
#include "user_interface.h"
}

// Déclarations matérielles
#define NBPIXELS1 7
#define NBPIXELS2 6
#define PINPIXELS1 D1
#define PINPIXELS2 D7
#define BOUTONJAUNE D3
#define BOUTONROUGE D4
#define BOUTONBLANC D5

Adafruit_NeoPixel pixels1(NBPIXELS1, PINPIXELS1 , NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel pixels2(NBPIXELS2, PINPIXELS2 , NEO_GRBW + NEO_KHZ800);

// Adresse MAC de l'émetteur PoulpyLights - BROADCAST pour compatibilité
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Structure du message envoyé
typedef struct {
  uint8_t type;            // 0xA0 = animation trigger
  uint8_t animationNumber; // numéro d'animation à déclencher
} AnimationTriggerMessage;

int animationNumber = 0;

// Tableau de couleurs RGB pour chaque animation (doit être identique à celui de l'émetteur)
const uint8_t animationColors[][3] = {
  {255, 0, 50},      // Animation 0 : rose
  {255, 0, 0},    // Animation 1 : rouge
  {0, 255, 0},    // Animation 2 : vert
  {0, 0, 255},    // Animation 3 : bleu
  {255, 255, 0},  // Animation 4 : jaune
  {0, 255, 255},  // Animation 5 : cyan
  {255, 0, 255},  // Animation 6 : magenta
  {255, 255, 255},// Animation 7 : blanc
  {128, 0, 255},  // Animation 8 : violet
  {255, 128, 0}   // Animation 9 : orange
};
const int NB_ANIMATIONS = sizeof(animationColors) / sizeof(animationColors[0]);

// --- Non-blocking animation system ---

// States for the main state machine
enum AnimationState {
  IDLE,
  STARTUP,
  TRIGGER
};
AnimationState currentState = IDLE;

// Timing and state variables for animations
unsigned long animationStartTime = 0;
unsigned long lastStepTime = 0;
int animationStep = 0;
uint32_t currentColor = 0;

// Debouncing variables
unsigned long lastDebounceTime = 0;
const long debounceDelay = 250;

// Function prototypes
void startStartupAnimation();
void updateStartupAnimation();
void startTriggerAnimation();
void updateTriggerAnimation();
void handleButtons();
void allLedsOff();
void OnDataSent(u8 *mac_addr, u8 status);
void OnDataRecv(u8 *mac, u8 *incomingData, u8 len);

void setup() {
  Serial.begin(115200);

  // Initialisation des boutons
  pinMode(BOUTONJAUNE, INPUT_PULLUP);
  pinMode(BOUTONROUGE, INPUT_PULLUP);
  pinMode(BOUTONBLANC, INPUT_PULLUP);

  // Initialisation des pixels (optionnel pour test visuel)
  pixels1.begin();
  pixels2.begin();
  allLedsOff();

  // Initialisation WiFi et ESP-NOW

  WiFi.disconnect();
  ESP.eraseConfig();

  WiFi.mode(WIFI_STA);
  // Laisser l'adresse MAC par défaut pour l'ONIRIGUN
  // setESP32MAC(0x8C, 0xBF, 0xEA, 0x88, 0x0B, 0xBC);
  // Forcer le canal 1 pour être synchronisé avec l'émetteur
  wifi_set_channel(1);
  delay(500); // Laisser le temps au WiFi de se stabiliser
  
  if (esp_now_init() != 0) {
    Serial.println("Erreur d'initialisation ESP-NOW");
    return;
  }
  
  // Callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Configuration peer broadcast
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  int result = esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  
  if (result != 0) {
    Serial.print("Erreur ajout peer: ");
    Serial.println(result);
    return;
  }

  Serial.println("=== ONIRIGUN DIAGNOSTIC ===");
  Serial.print("Canal WiFi : ");
  Serial.println(wifi_get_channel());
  Serial.print("Adresse MAC ONIRIGUN : ");
  Serial.println(WiFi.macAddress());
  Serial.print("Adresse emetteur cible : ");
  for (int i = 0; i < 6; i++) {
    if (broadcastAddress[i] < 16) Serial.print("0"); // Ajouter 0 pour les valeurs < 16
    Serial.print(broadcastAddress[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.print("Peer ajouté sur canal : ");
  Serial.println(1);
  Serial.println("Onirigun prêt !");
  
  // Test de communication ESP-NOW au démarrage
  Serial.println("Test de communication ESP-NOW...");
  uint8_t testData[2] = {0x99, 0x88};
  int testResult = esp_now_send(broadcastAddress, testData, sizeof(testData));
  Serial.print("Test result : ");
  Serial.println(testResult);
  
  // Diagnostic supplémentaire (API ESP8266)
  // esp_now_get_peer_num() n'existe pas sur ESP8266
  
  // Vérifier si le peer existe (API ESP8266)
  // if (esp_now_is_peer_exist(emitterAddress)) {
  //   Serial.println("Peer emetteur trouvé dans la liste");
  // } else {
  //   Serial.println("ERREUR: Peer emetteur NOT FOUND dans la liste !");
  // }
  // Allumer l'Onirigun avec la première couleur au démarrage
  uint8_t r = animationColors[animationNumber][0];
  uint8_t g = animationColors[animationNumber][1];
  uint8_t b = animationColors[animationNumber][2];
  currentColor = pixels1.Color(r, g, b);
  startStartupAnimation();
}

void loop() {
  handleButtons();

  switch(currentState) {
    case STARTUP:
      updateStartupAnimation();
      break;
    case TRIGGER:
      updateTriggerAnimation();
      break;
    case IDLE:
    default:
      // Do nothing
      break;
  }
}

void handleButtons() {
  // Only check for buttons if enough time has passed since the last press
  if (millis() - lastDebounceTime < debounceDelay) {
    return;
  }

  uint8_t r, g, b;

  if (digitalRead(BOUTONJAUNE) == LOW) {
    animationNumber++;
    if (animationNumber >= NB_ANIMATIONS) animationNumber = 0;
    Serial.print("Animation : "); Serial.println(animationNumber);
    r = animationColors[animationNumber][0];
    g = animationColors[animationNumber][1];
    b = animationColors[animationNumber][2];
    currentColor = pixels1.Color(r, g, b);
    startStartupAnimation();
    lastDebounceTime = millis();
  }
  else if (digitalRead(BOUTONROUGE) == LOW) {
    animationNumber--;
    if (animationNumber < 0) animationNumber = NB_ANIMATIONS - 1;
    Serial.print("Animation : "); Serial.println(animationNumber);
    r = animationColors[animationNumber][0];
    g = animationColors[animationNumber][1];
    b = animationColors[animationNumber][2];
    currentColor = pixels1.Color(r, g, b);
    startStartupAnimation();
    lastDebounceTime = millis();
  }
  else if (digitalRead(BOUTONBLANC) == LOW) {
    AnimationTriggerMessage msg = {0xA0, (uint8_t)animationNumber};
    esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(msg));
    Serial.print("Trigger envoyé, animation : "); Serial.println(animationNumber);
    
    r = animationColors[animationNumber][0];
    g = animationColors[animationNumber][1];
    b = animationColors[animationNumber][2];
    currentColor = pixels1.Color(r, g, b);
    startTriggerAnimation();
    lastDebounceTime = millis();
  }
}

void allLedsOff() {
    pixels1.clear();
    pixels2.clear();
    pixels1.show();
    pixels2.show();
}

void startStartupAnimation() {
  currentState = STARTUP;
  animationStep = 0;
  lastStepTime = millis();
  allLedsOff();
}

void updateStartupAnimation() {
  unsigned long currentTime = millis();
  const int ledOnTime = 100;
  const int stayOnTime = 1000;

  // Step through pixels1
  if (animationStep < NBPIXELS1) {
    if (currentTime - lastStepTime > ledOnTime) {
      pixels1.setPixelColor(animationStep, currentColor);
      pixels1.show();
      animationStep++;
      lastStepTime = currentTime;
    }
    return;
  }

  // Step through pixels2
  if (animationStep < NBPIXELS1 + NBPIXELS2) {
    if (currentTime - lastStepTime > ledOnTime) {
      pixels2.setPixelColor(animationStep - NBPIXELS1, currentColor);
      pixels2.show();
      animationStep++;
      lastStepTime = currentTime;
    }
    return;
  }

  // Keep all leds on
  if (animationStep == NBPIXELS1 + NBPIXELS2) {
    if (currentTime - lastStepTime > stayOnTime) {
      allLedsOff();
      currentState = IDLE;
    }
    return;
  }
}

void startTriggerAnimation() {
  currentState = TRIGGER;
  animationStartTime = millis();
  animationStep = 0;
  lastStepTime = millis();
  allLedsOff(); // Éteindre toutes les LEDs immédiatement
}

void updateTriggerAnimation() {
  const int quickSequenceDuration = 250; // 250ms pour la séquence rapide
  const int fadeDuration = 1000; // 1 seconde pour le fondu
  const int totalLeds = NBPIXELS1 + NBPIXELS2; // 13 LEDs total
  const int ledOnTime = quickSequenceDuration / totalLeds; // ~19ms par LED
  
  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - animationStartTime;

  // Phase 1 & 2: Rallumage séquentiel rapide (250ms)
  if (elapsedTime < quickSequenceDuration) {
    // Allumer les LEDs une à une très rapidement
    if (currentTime - lastStepTime > ledOnTime) {
      if (animationStep < NBPIXELS1) {
        // Allumer pixels1
        pixels1.setPixelColor(animationStep, currentColor);
        pixels1.show();
      } else if (animationStep < totalLeds) {
        // Allumer pixels2
        pixels2.setPixelColor(animationStep - NBPIXELS1, currentColor);
        pixels2.show();
      }
      
      animationStep++;
      lastStepTime = currentTime;
    }
    return;
  }
  
  // Phase 3: Fondu (2 secondes après la séquence rapide)
  unsigned long fadeElapsedTime = elapsedTime - quickSequenceDuration;
  
  if (fadeElapsedTime >= fadeDuration) {
    allLedsOff();
    currentState = IDLE;
    return;
  }

  // Calculer la luminosité pour le fondu
  float brightness = 1.0f - ((float)fadeElapsedTime / (float)fadeDuration);

  uint8_t r = (currentColor >> 16) & 0xFF;
  uint8_t g = (currentColor >> 8) & 0xFF;
  uint8_t b = currentColor & 0xFF;

  uint32_t fadedColor = pixels1.Color((uint8_t)(r * brightness), (uint8_t)(g * brightness), (uint8_t)(b * brightness));

  pixels1.fill(fadedColor);
  pixels2.fill(fadedColor);
  pixels1.show();
  pixels2.show();
}

// Callback d'envoi ESP-NOW
void OnDataSent(u8 *mac_addr, u8 status) {
  Serial.print("Envoi vers ");
  for (int i = 0; i < 6; i++) {
    if (mac_addr[i] < 16) Serial.print("0");
    Serial.print(mac_addr[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" - Status: ");
  
  if (status == 0) {
    Serial.println("SUCCÈS");
  } else {
    Serial.print("ÉCHEC (");
    Serial.print(status);
    Serial.println(")");
  }
}

// Callback de réception ESP-NOW (pour recevoir les données DMX de Poulpylights)
void OnDataRecv(u8 *mac, u8 *incomingData, u8 len) {
  Serial.print("Données DMX reçues de ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" - Taille: ");
  Serial.print(len);
  
  if (len >= 3) {
    Serial.print(" - DMX[1]: ");
    Serial.print(incomingData[1]);
    Serial.print(", DMX[2]: ");
    Serial.println(incomingData[2]);
  } else {
    Serial.println(" bytes");
  }
}




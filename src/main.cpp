/***********************************************************************
 Ce code reçoit des données DMX via Art-Net (UDP) 
 et pilote un ruban WS2812 (144 LED). 
 Il garde la même logique de modes DMX que l'ancien code (DMX2LEDSTRIP).
 Au lieu d'ESP-NOW, nous utilisons ArtnetWifi pour remplir dmxChannels[512].
************************************************************************/

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#include <FastLED.h>
#include "OneButton.h"

// Bibliothèque Art-Net 
#include <ArtnetWifi.h>

/***********************************************************************
 *                          DEFINES / CONST
 ***********************************************************************/

// Broches
#define DATA_PIN            D2    // pin de contrôle du strip led
#define BUTTONPIN           D1    // bouton (pullup interne)
#define BUTTONGROUNDPIN     D5    // on s'en sert pour fournir GND au bouton

// Longueur du strip
#define MAXLEDLENGTH        144

// Nombre de groupes (utilisé dans vos modes)
#define NBGROUPS            10

// Quelques constantes 
#define EEPROM_SIZE         32
#define VERSION             15

// Paramètres pour modes d'animation
#define DEGRADE_FACTOR      0.5
#define FADE_SPEED          0.02

// États
#define RUNNING true
#define SETUP   false


/***********************************************************************
 *                         Variables globales
 ***********************************************************************/

// Tableau qui stocke les 512 canaux DMX (index 0=canal 1 DMX)
uint8_t dmxChannels[512];

// Leds
CRGB leds[MAXLEDLENGTH];
CRGB flickerLeds[MAXLEDLENGTH];
uint8_t ledsTemp[MAXLEDLENGTH][3];

// WiFi manager
WiFiManager wifiManager;

// Bouton
OneButton button1(BUTTONPIN, true); // "true" = active internal pullup

// Numéro de groupe, mode, etc.
int setupAddress = 1;
int setupMode = 1;
int setupTubeNumber = 1;
bool etat = RUNNING; // RUNNING ou SETUP

// Variables pour vos effets
double lastOffset = 0;
double lastOffsetR = 0, lastOffsetG = 0, lastOffsetB = 0;
double offsetInc   = 0.1;
double offsetIncR  = 0.1;
double offsetIncG  = 0.1;
double offsetIncB  = 0.1;

uint8_t tempr = 0, tempg = 0, tempb = 0;
unsigned long lastFlicker = 0;
unsigned long debutAllume = 0;
unsigned long lastUpdateFlickerPattern = 0;

uint8_t hueOffset       = 0;
uint8_t randomHueOffset = 0;

int flashInterval;

// Objet Art-Net
ArtnetWifi artnet;


/***********************************************************************
 *                             FONCTIONS
 ***********************************************************************/

// Callback Art-Net : chaque fois qu'on reçoit un packet DMX
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  // Ici, 'data' contient 'length' octets de données DMX pour l'univers 'universe'.
  // On recopie dans dmxChannels (max 512 canaux).
  // Si vous ne gérez qu'un seul univers, c'est facile. 
  // S'il y a plusieurs univers, vous pouvez gérer un offset : 
  //   e.g. if universe == 0 => offset 0 
  //        if universe == 1 => offset 512, etc.
  // Pour un récepteur unique, on suppose un seul univers DMX (0 ou 1).

  // Simple exemple : univers = 0
  int offset = 0; 
  // ou si vous voulez "universe 1" => offset=0, 
  //   => if(universe==1) offset=0; 
  //   => if(universe==2) offset=512; (pour 2 univers)
  // etc.

  for (int i = 0; i < length; i++) {
    // Ne pas dépasser 512
    if (i + offset < 512) {
      dmxChannels[i + offset] = data[i];
    }
  }
}

// convertToColor, flickering, etc. 
// => on reprend vos fonctions existantes telles quelles
struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

void blinkLeds(int numberOfBlinks, int blinkLength, int blinkR, int blinkG, int blinkB)
{
  for (int j = 0; j<MAXLEDLENGTH; j += 5)
      {
        leds[j].r = blinkR;
        leds[j].g = blinkG;
        leds[j].b = blinkB;
      }
  for(int i = 0;i<numberOfBlinks;i++)
  {
    FastLED.show();
    delay(blinkLength);
    FastLED.clear();
    FastLED.show();
    delay(blinkLength);
    }
    
}

Color convertToColor(uint8_t index)
{
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

/*********************************************************************/
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
/*********************************************************************/
// DMX2LEDSTRIP - inchangée dans l'ensemble, sauf qu'on a retiré 
// les références directes à ESP-NOW dans les commentaires. 
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

// Bouton : click => incremente le numéro de groupe si en SETUP
void click1()
{
  if (etat == RUNNING) return;
  setupTubeNumber = (setupTubeNumber + 1) % NBGROUPS;
}

// Bouton : long press => bascule RUNNING <-> SETUP
void longPressStart1()
{
  Serial.print("longpress | etat = ");
  Serial.println((etat ? "RUNNING" : "SETUP"));

  if (etat == SETUP) // on quitte SETUP => on sauvegarde en EEPROM
  {
    EEPROM.write(0, setupAddress);
    EEPROM.write(4, setupMode);
    EEPROM.write(8, setupTubeNumber);
    Serial.print("Commit =  ");
    Serial.println(EEPROM.commit());
  }

  etat = !etat;
}


/***********************************************************************
 *                              SETUP
 ***********************************************************************/ 
void setup()
{
  randomHueOffset = rand()%256;
  Serial.begin(115200);
  Serial.println("\n=== Récepteur DMX via Art-Net ===");
  Serial.print("Version ");
  Serial.println(VERSION);

  pinMode(BUTTONGROUNDPIN, OUTPUT);
  digitalWrite(BUTTONGROUNDPIN, LOW);

  // Init LED
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, MAXLEDLENGTH);

  // Setup bouton OneButton
  button1.attachClick([](){
    if(etat==RUNNING) return;
    setupTubeNumber = (setupTubeNumber+1) % NBGROUPS;
  });
  button1.attachLongPressStart([](){
    Serial.printf("longpress | etat=%s\n", etat?"RUNNING":"SETUP");
    if(etat==SETUP) {
      // sauver en EEPROM
      EEPROM.write(0, setupAddress);
      EEPROM.write(4, setupMode);
      EEPROM.write(8, setupTubeNumber);
      Serial.printf("Commit= %d\n", (int)EEPROM.commit());
    }
    etat = !etat;
  });

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  setupAddress   = EEPROM.read(0);
  setupMode      = EEPROM.read(4);
  setupTubeNumber= EEPROM.read(8);

  if((setupAddress<1)||(setupAddress>512)) setupAddress=1;
  if((setupMode<1)||(setupMode>255))       setupMode=1;
  if((setupTubeNumber<0)||(setupTubeNumber>32)) setupTubeNumber=0;

  Serial.printf("Paramètres EEPROM: Addr=%d | Mode=%d | TubeGroup=%d\n", 
                setupAddress, setupMode, setupTubeNumber);

  //================= Gestion Wifi Manager ====================//

  // 1) Déterminer si on force le portail de config
  bool forceConfigPortal = false;

  // Lire l'état du bouton (OneButton a un buffer; 
  //  mais on peut juste faire digitalRead(BUTTONPIN) en direct, 
  //  ou paramétrer la pin autrement)
  pinMode(BUTTONPIN, INPUT_PULLUP);
  delay(50); // petite attente
  if(digitalRead(BUTTONPIN)==LOW) {
    // le bouton est appuyé au démarrage
    forceConfigPortal = true;
    Serial.println("==> Bouton appuyé, on forcera le portail WiFiManager");
  }

  // 2) Config WifiManager 
  // param optionnels
  
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(true);

  if(forceConfigPortal) {
    // 2.a) Lancement direct du config portal
    // clignotement bleu => 3 blinks, 1 seconde
    blinkLeds(3, 1000, 0,0,255);
    Serial.println("Démarrage du portail WiFiManager (FORCE)");
    wifiManager.startConfigPortal("Artnet-Receiver"); 
    // => Bloque jusqu’à ce qu’on ait configuré un réseau. 
  } else {
    // 2.b) On tente autoConnect => 
    //    si pas de réseau connu -> ouvre un portail 
    //    sinon connecte direct
    if(!wifiManager.autoConnect("Artnet-Receiver")) {
      // si on sort d’autoconnect sans succès => 
      //   on peut re-tenter ou faire un reset
      Serial.println("Echec d’autoConnect, on lance un portail quand même");
      blinkLeds(3, 1000, 0,0,255);
      wifiManager.startConfigPortal("Artnet-Receiver"); 
    }
  }

  // A ce stade, on est connecté ou on a configuré un nouveau wifi
  // => suite
  Serial.print("WiFi connecté, IP=");
  Serial.println(WiFi.localIP());

  // ================ Démarrage de l’Art-Net ================
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);

  // Petit blink pour signaler tout OK
  blinkLeds(2,300, 255,255,0);

  // Fin du setup
}


/***********************************************************************
 *                              LOOP
 ***********************************************************************/
void loop()
{
  button1.tick();  // gestion du bouton

  // Lire les packets Art-Net
  artnet.read();

  if (etat == RUNNING)
  {
    // Appliquer les valeurs DMX à votre ruban LED
    DMX2LEDSTRIP();
  }
  else
  {
    // SETUP => clignoter un certain nombre de LEDs
    FastLED.clear();
    if (setupTubeNumber == 0) {
      leds[0].r = 250; 
    } else {
      for (int j = 1; j <= NBGROUPS * setupTubeNumber; j += NBGROUPS)
      {
        leds[j].r = 0;
        leds[j].g = 250;
        leds[j].b = 0;
      }
    }
    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }
}

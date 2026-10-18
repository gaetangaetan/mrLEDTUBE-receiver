#include "power_budget.h"

#include <EEPROM.h>
#include <FastLED.h>

// 10,5 W pour une couleur à 255 sur 144 LED. Linéaire.
static const uint32_t MW_PER_FULL_CHANNEL = 10500;
static const uint32_t REFERENCE_LEDS = 144;
// 0 = pas de limite. L'utilisateur la pose depuis Pollux ou PoulpyNode (watts, max 200).
static uint16_t powerLimitW = 0;

static const int EEPROM_ADDR_LIMIT_MAGIC = 253;
static const int EEPROM_ADDR_LIMIT_W = 254;
static const uint8_t LIMIT_MAGIC = 0xE2;
static const uint32_t SAVE_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Après l'URL OTA (102 + 1 + 140 = 243).
static const int EEPROM_ADDR_ENERGY_MAGIC = 248;
static const int EEPROM_ADDR_ENERGY_MWH = 249;
static const uint8_t ENERGY_MAGIC = 0xE1;

extern CRGB leds[];
extern uint16_t ledCount;

static uint32_t energyMwh = 0;
static uint32_t residueMwMs = 0;
static uint32_t instantMw = 0;
static uint32_t lastMs = 0;
static uint32_t lastSaveMs = 0;
static uint32_t savedMwh = 0;

static void writeEnergy(uint32_t value) {
  EEPROM.write(EEPROM_ADDR_ENERGY_MAGIC, ENERGY_MAGIC);
  EEPROM.write(EEPROM_ADDR_ENERGY_MWH, value & 0xFF);
  EEPROM.write(EEPROM_ADDR_ENERGY_MWH + 1, (value >> 8) & 0xFF);
  EEPROM.write(EEPROM_ADDR_ENERGY_MWH + 2, (value >> 16) & 0xFF);
  EEPROM.write(EEPROM_ADDR_ENERGY_MWH + 3, (value >> 24) & 0xFF);
  EEPROM.commit();
  savedMwh = value;
  lastSaveMs = millis();
}

void powerBudgetLoad() {
  lastMs = millis();
  lastSaveMs = lastMs;
  if (EEPROM.read(EEPROM_ADDR_ENERGY_MAGIC) == ENERGY_MAGIC) {
    energyMwh = (uint32_t)EEPROM.read(EEPROM_ADDR_ENERGY_MWH)
              | ((uint32_t)EEPROM.read(EEPROM_ADDR_ENERGY_MWH + 1) << 8)
              | ((uint32_t)EEPROM.read(EEPROM_ADDR_ENERGY_MWH + 2) << 16)
              | ((uint32_t)EEPROM.read(EEPROM_ADDR_ENERGY_MWH + 3) << 24);
    savedMwh = energyMwh;
  }
  if (EEPROM.read(EEPROM_ADDR_LIMIT_MAGIC) == LIMIT_MAGIC) {
    powerLimitW = (uint16_t)EEPROM.read(EEPROM_ADDR_LIMIT_W)
                | ((uint16_t)EEPROM.read(EEPROM_ADDR_LIMIT_W + 1) << 8);
    if (powerLimitW > 200) powerLimitW = 200;
  }
}

uint32_t powerBudgetEnergyMwh() { return energyMwh; }
uint32_t powerBudgetInstantMw() { return instantMw; }
uint16_t powerBudgetLimitW() { return powerLimitW; }

void powerBudgetSetLimitW(uint16_t watts) {
  if (watts > 200) watts = 200;
  powerLimitW = watts;
  EEPROM.write(EEPROM_ADDR_LIMIT_MAGIC, LIMIT_MAGIC);
  EEPROM.write(EEPROM_ADDR_LIMIT_W, watts & 0xFF);
  EEPROM.write(EEPROM_ADDR_LIMIT_W + 1, (watts >> 8) & 0xFF);
  EEPROM.commit();
}

void powerBudgetReset() {
  energyMwh = 0;
  residueMwMs = 0;
  instantMw = 0;
  writeEnergy(0);
}

static const uint16_t LED_BUFFER = 300;

static uint32_t channelSum() {
  uint16_t n = ledCount;
  if (n > LED_BUFFER) n = LED_BUFFER;
  uint32_t sum = 0;
  for (uint16_t i = 0; i < n; i++)
    sum += (uint32_t)leds[i].r + leds[i].g + leds[i].b;
  return sum;
}

static uint32_t powerFromSum(uint32_t sum) {
  uint32_t bri = FastLED.getBrightness();
  // mW = 10500 * somme * luminosité / (144 * 255 * 255)
  uint64_t num = (uint64_t)MW_PER_FULL_CHANNEL * sum * bri;
  uint32_t den = REFERENCE_LEDS * 255u * 255u;
  return (uint32_t)(num / den);
}

static void scaleToLimit(uint32_t powerMw, uint32_t limitMw) {
  uint16_t n = ledCount;
  if (n > LED_BUFFER) n = LED_BUFFER;
  for (uint16_t i = 0; i < n; i++) {
    leds[i].r = (uint8_t)((uint32_t)leds[i].r * limitMw / powerMw);
    leds[i].g = (uint8_t)((uint32_t)leds[i].g * limitMw / powerMw);
    leds[i].b = (uint8_t)((uint32_t)leds[i].b * limitMw / powerMw);
  }
}

static void account(uint32_t powerMw) {
  uint32_t now = millis();
  uint32_t dt = now - lastMs;
  lastMs = now;
  if (dt > 2000) dt = 0;
  uint64_t acc = (uint64_t)residueMwMs + (uint64_t)powerMw * dt;
  energyMwh += (uint32_t)(acc / 3600000ULL);
  residueMwMs = (uint32_t)(acc % 3600000ULL);
}

// Toutes les 5 min, allumé ou noir. Sinon un arrêt batterie efface la fin du compteur.
static void saveDue() {
  if (energyMwh == savedMwh) return;
  if (millis() - lastSaveMs < SAVE_INTERVAL_MS) return;
  writeEnergy(energyMwh);
}

void poulpyShow() {
  uint32_t sum = channelSum();
  uint32_t powerMw = powerFromSum(sum);
  uint32_t limitMw = (uint32_t)powerLimitW * 1000u;
  if (powerLimitW > 0 && powerMw > limitMw) {
    scaleToLimit(powerMw, limitMw);
    powerMw = limitMw;
  }
  instantMw = powerMw;
  account(powerMw);
  saveDue();
  FastLED.show();
}

#ifndef POWER_BUDGET_H
#define POWER_BUDGET_H

#include <stdint.h>

// À appeler après EEPROM.begin().
void powerBudgetLoad();

// Limite la puissance, cumule l'énergie, puis envoie le ruban.
void poulpyShow();

uint32_t powerBudgetEnergyMwh();
uint32_t powerBudgetInstantMw();
uint16_t powerBudgetLimitW();
void powerBudgetSetLimitW(uint16_t watts);
void powerBudgetReset();

#endif

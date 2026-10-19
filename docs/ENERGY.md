# Compteur d’énergie — PoulpyTube

Le tube estime sa consommation. Ce n’est pas une mesure de courant.

## Modèle

10,5 W pour **une** couleur à 255 sur 144 LED, linéaire avec la somme R+G+B et la luminosité FastLED. Le nombre de LED change le résultat. Fichier : `src/power_budget.cpp`.

L’énergie s’accumule en RAM (`mWh`). La puissance instantanée est en `mW`.

## GET_INFO

| `data[]` | Contenu |
|----------|---------|
| 56–59 | mWh cumulés, uint32 little-endian |
| 60–63 | mW instantanés, uint32 little-endian |
| 64–65 | limite en watts, uint16 little-endian. 0 = aucune |

## EEPROM

| Adresse | Rôle |
|---------|------|
| 248 | magic énergie `0xE1` |
| 249–252 | mWh, uint32 little-endian |
| 253 | magic limite `0xE2` |
| 254–255 | limite en watts |

Écriture au plus toutes les **5 minutes**, que la barre soit allumée ou noire. Firmware **261019-0124**. Avant cette version, l’écriture n’avait lieu que barre noire : couper les piles en pleine charge pouvait perdre la fin du compteur.

## Reset et limite

| Commande | Octet | Effet |
|----------|-------|--------|
| `RESET_ENERGY` | `0xF0` | RAM et EEPROM à 0 |
| `SET_POWER_LIMIT` | `0xF1` + uint16 watts | plafond 0–200. Au-dessus, R, G et B baissent ensemble |

Le plein affiché par Pollux et par la page du Node est **9500 mWh**. Il n’est pas dans le firmware. C’est le résultat d’une décharge à 10 W (compteur arrêté vers 9647 mWh). Après un changement de piles, il faut un reset : le compteur ne voit pas la recharge.

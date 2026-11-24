/* SPDX-License-Identifier: MIT */
#ifndef S2_CSA2_H
#define S2_CSA2_H
#include <stdint.h>
/* Bluetooth LE Channel Selection Algorithm #2. 255 means an invalid map. */
uint8_t s2_csa2_channel(const uint8_t map[5],uint16_t event,uint16_t identifier);
#endif

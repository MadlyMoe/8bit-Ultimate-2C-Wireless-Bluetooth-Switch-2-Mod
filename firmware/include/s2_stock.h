/* SPDX-License-Identifier: MIT */
#ifndef S2_STOCK_H
#define S2_STOCK_H
#include "s2_bond_store.h"
#include "s2_input_301b.h"
/* Wireless 301B v1.09 ONLY; link these only after exact stock-hash verification.
 * These adapters have static ABI evidence but have not executed on hardware. */
int s2_stock_ltk_reply(uint16_t connection,const s2_saved_bond *bond,
    uint8_t peer_type,const uint8_t peer[6],const uint8_t random[8],uint16_t ediv);
void s2_stock_motor(uint8_t left,uint8_t right);
void s2_stock_motor_stop(void);
void s2_stock_read_calibrated(s2_calibrated_301b *);
uint16_t s2_stock_battery_mv(void);
uint8_t s2_stock_battery_percent(void);
#endif

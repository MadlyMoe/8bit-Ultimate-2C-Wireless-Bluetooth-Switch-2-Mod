/* SPDX-License-Identifier: MIT */
#include "s2_stock.h"

int s2_stock_ltk_reply(uint16_t connection,const s2_saved_bond *bond,
    uint8_t peer_type,const uint8_t peer[6],const uint8_t random[8],uint16_t ediv) {
    unsigned i;
    uint8_t key[16],nonzero=0;
    int (*reply)(uint16_t,const uint8_t *)=(int (*)(uint16_t,const uint8_t *))0x130d9u;
    int (*reject)(uint16_t)=(int (*)(uint16_t))0x13115u;
    /* Nintendo provisions a legacy LTK with zero EDIV/RAND. Never give a saved
     * key to a different peer, including a fresh uncommitted pairing attempt. */
    if(random) for(i=0;i<8;++i) nonzero|=random[i];
    if(!random || ediv || nonzero || !s2_bond_for_peer(bond,peer_type,peer)) {
        reject(connection); return S2_SEQUENCE;
    }
    /* HCI LE LTK Request Reply takes A1 XOR B1 without a swap. The reversal in
     * the proprietary confirmation calculation is NOT the HCI key order. */
    for(i=0;i<16;++i) key[i]=bond->protocol.ltk_wire[i];
    i=(unsigned)reply(connection,key);
    /* LL owns its private copy now; do not leave an extra stack copy. */
    { volatile uint8_t *wipe=key; unsigned n; for(n=0;n<16;++n) wipe[n]=0; }
    return i?S2_BACKEND:S2_OK;
}
void s2_stock_motor(uint8_t left,uint8_t right) {
    void (*drive)(uint8_t,uint8_t)=(void (*)(uint8_t,uint8_t))0xf93du;
    /* Keep the shared stock main-loop applier consistent. Its timed buzz latch
     * otherwise refuses zero-duration changes, including disconnect-stop. */
    *(volatile uint8_t *)0x8434f1u=0;
    /* Native driver uses 0..255, despite the stock HID descriptor's 0..100. */
    *(volatile uint8_t *)0x8434efu=(uint8_t)(((left>100?100u:left)*255u+50u)/100u);
    *(volatile uint8_t *)0x8434f0u=(uint8_t)(((right>100?100u:right)*255u+50u)/100u);
    drive(*(volatile uint8_t *)0x8434efu,0);
    drive(*(volatile uint8_t *)0x8434f0u,1);
}
void s2_stock_motor_stop(void) { s2_stock_motor(0,0); }
void s2_stock_read_calibrated(s2_calibrated_301b *s) {
    /* Main-loop only, after stock scanner/calibration; not from radio IRQ. */
    s->buttons=*(volatile uint32_t *)0x84342cu;
    s->axes[0]=*(volatile uint16_t *)0x8433ecu;
    s->axes[1]=*(volatile uint16_t *)0x8433f6u;
    s->axes[2]=*(volatile uint16_t *)0x8433e4u;
    s->axes[3]=*(volatile uint16_t *)0x8433e6u;
    s->zl=*(volatile uint8_t *)0x84340eu;
    s->zr=*(volatile uint8_t *)0x8433e8u;
}
uint16_t s2_stock_battery_mv(void) {
    uint32_t mv=*(volatile uint32_t *)0x843424u;
    return (uint16_t)(mv>65535u?65535u:mv);
}
uint8_t s2_stock_battery_percent(void) {
    uint8_t (*read_percent)(void)=(uint8_t (*)(void))0xb5a9u;
    return read_percent();
}

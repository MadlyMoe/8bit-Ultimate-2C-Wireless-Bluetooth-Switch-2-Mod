/* SPDX-License-Identifier: MIT
 * B80 radio bindings derived from the exact v1.09 image and Telink's B80 SDK.
 * TLSR8258 radio register sequences are deliberately not used here.
 */
#include "runtime_301b.h"
#include "s2_csa2.h"
#include <string.h>
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|((unsigned)p[1]<<8)); }
void s2_301b_phy_switch(uint8_t phy) {
    if(phy==2) {
        R32(0x801220u)=0x06432a04u;R8(0x800402u)=0x4c;R8(0x800404u)=0xe5;
        R32(0x800460u)=0x61514636u;R16(0x800464u)=0x786d;
        R8(0x842ed8u)=2;R8(0x842edbu)=4;R16(0x842edcu)=24;
    } else {
        R32(0x801220u)=0x23200a16u;R8(0x800402u)=0x46;R8(0x800404u)=0xf5;
        R32(0x800460u)=0x5f4f4434u;R16(0x800464u)=0x766b;
        R8(0x842ed8u)=1;R8(0x842edbu)=8;R16(0x842edcu)=40;
    }
}
static void phy_apply(void) {
    uint8_t next=R8(0x843b0eu);
    if(next!=1 && next!=2) { s2_link.failed=1;return; }
    if(R8(0x843b0du)!=next) {
        R8(0x843b0du)=next;R32(0x843598u)|=0x80;
    }
    R8(0x843b0fu)=0;
    if(R8(0x842ed8u)!=next) s2_301b_phy_switch(next);
}
void s2_301b_phy_init(void) {
    R32(0x8435b0u)=(uintptr_t)s2_301b_phy_switch;
    R32(0x8435b4u)=(uintptr_t)phy_apply;
    R8(0x843b0cu)=3;R8(0x843b0du)=1;R8(0x843b0eu)=1;R8(0x843b0fu)=0;
    R32(0x842e6cu)|=0x4100u; /* LE 2M + CSA2, with both implementations installed. */
    s2_301b_phy_switch(1);
}
void s2_301b_phy_instant(const uint8_t *ll) {
    uint8_t current=R8(0x843b0du),rx,tx;
    uint16_t instant,distance;
    if(ll[1]!=5 || ll[2]!=0x18) { s2_link.failed=1;return; }
    rx=ll[3]?ll[3]:current;tx=ll[4]?ll[4]:current;
    if(rx!=tx || (rx!=1 && rx!=2)) { s2_link.failed=1;return; }
    if(rx==current) return;
    instant=le16(ll+5);distance=(uint16_t)(instant-R16(0x843ad4u));
    if(!distance || distance>=0x8000u) { s2_link.failed=1;return; }
    if(R8(0x843ac8u) && !(R8(0x843ac8u)==3 && R16(0x843ad6u)==instant)) {
        s2_link.failed=1;return;
    }
    R8(0x843b0eu)=rx;R8(0x843b0fu)=1;
    R16(0x843ad6u)=instant;R8(0x843ac8u)=3;R8(0x843b23u)=1;
}
void s2_301b_phy_control(const uint8_t *ll) {
    s2_link.trace.flags|=S2_TRACE_PHY_REQUEST;
    if(ll[2]==0x18) { s2_301b_phy_instant(ll);return; }
    if(ll[1]!=3 || !(ll[3]&3) || !(ll[4]&3)) { s2_link.failed=1;return; }
    if(ll[2]==0x16) s2_link.phy_reply_pending=1;
    /* As a peripheral, wait for the central's LL_PHY_UPDATE_IND instant. */
}
void s2_301b_phy_poll(void) {
    static const uint8_t response[5]={3,3,0x17,3,3};
    /* This LL enqueue returns ONE on success; unlike L2CAP's zero status.
     * Retain the response when its eight-slot FIFO is temporarily full. */
    if(s2_link.phy_reply_pending &&
       FN(0x122fcu,int,(uint16_t,const uint8_t *))(0x8040,response)==1)
        s2_link.phy_reply_pending=0;
}
void s2_301b_connect_packet(const uint8_t *dma) {
    s2_link.csa2=(dma[4]&0x20)!=0;
    s2_unit.peer_type=(dma[4]>>6)&1;memcpy(s2_unit.peer,dma+6,6);
    FN(0x12cf8u,void,(const uint8_t *))(dma);
}
uint8_t s2_301b_channel(volatile uint8_t *connection) {
    uint8_t result;
    if(!s2_link.csa2) return connection[5];
    /* Stock 0x12052 copies the CONNECT_IND access address to +16 in wire
     * order. The reversed copy at +20 is for the radio registers. */
    result=s2_csa2_channel((const uint8_t *)connection+7,
        (uint16_t)(connection[12]|((unsigned)connection[13]<<8)),
        (uint16_t)(connection[16]|((unsigned)connection[17]<<8))^
        (uint16_t)(connection[18]|((unsigned)connection[19]<<8)));
    if(result>36) { s2_link.failed=1;return connection[5]; }
    connection[5]=result;return result;
}

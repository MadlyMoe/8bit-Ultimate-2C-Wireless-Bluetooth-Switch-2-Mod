/* SPDX-License-Identifier: MIT
 * Bluetooth Core Vol 6 Part B 4.5.8.3; see THIRD_PARTY.md for reference code.
 */
#include "s2_csa2.h"
static uint16_t permute(uint16_t v) {
    v=(uint16_t)(((v&0xaaaau)>>1)|((v&0x5555u)<<1));
    v=(uint16_t)(((v&0xccccu)>>2)|((v&0x3333u)<<2));
    return (uint16_t)(((v&0xf0f0u)>>4)|((v&0x0f0fu)<<4));
}
uint8_t s2_csa2_channel(const uint8_t map[5],uint16_t event,uint16_t identifier) {
    uint16_t prn=(uint16_t)(event^identifier);
    unsigned i,count=0,index,unmapped;
    for(i=0;i<3;++i) prn=(uint16_t)(17u*permute(prn)+identifier);
    prn^=identifier;unmapped=prn%37u;
    for(i=0;i<37;++i) if(map[i>>3]&(1u<<(i&7))) ++count;
    if(count<2 || (map[4]&0xe0)) return 255;
    if(map[unmapped>>3]&(1u<<(unmapped&7))) return (uint8_t)unmapped;
    index=(count*(uint32_t)prn)>>16;
    for(i=0;i<37;++i) if(map[i>>3]&(1u<<(i&7))) {
        if(index==0) return (uint8_t)i;
        --index;
    }
    return 255;
}

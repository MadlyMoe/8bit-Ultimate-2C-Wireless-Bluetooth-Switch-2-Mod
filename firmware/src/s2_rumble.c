/* SPDX-License-Identifier: MIT
 * Inverse of the documented Switch 2 40-bit sample format (SDL encoder and
 * nxnative switch2_proto.h research). Independently implemented for ERM motors.
 */
#include "s2_protocol.h"
#include "s2_rumble.h"
#include <string.h>

int s2_decode_rumble(const uint8_t *packet, size_t len, s2_rumble *out) {
    s2_rumble next;
    unsigned side,i;
    if (!packet || !out) return S2_INVALID;
    if (len<33) return S2_SHORT;
    if (packet[0]!=0) return S2_INVALID; /* Bluetooth report prefix */
    memset(&next,0,sizeof(next));
    for (side=0; side<2; ++side) {
        const uint8_t *block=packet+1+16*side;
        uint8_t count=(block[0]>>4)&3;
        if (!block[0]) {
            for (i=1;i<16;++i) if (block[i]) return S2_INVALID;
            continue;
        }
        if ((block[0]&0xc0)!=0x40 || !count) return S2_INVALID;
        next.count[side]=count; next.sequence[side]=block[0]&15;
        for (i=0;i<count;++i) {
            const uint8_t *p=block+1+5*i;
            unsigned high=(unsigned)(p[1]>>2)+((unsigned)(p[2]&15)<<6);
            unsigned low=(unsigned)(p[3]>>6)+((unsigned)p[4]<<2);
            unsigned total=high+low;
            if (total>1023) total=1023;
            next.strength[side][i]=(uint8_t)((total*100u+511u)/1023u);
        }
    }
    *out=next;
    return S2_OK;
}
void s2_rumble_stop(s2_rumble_player *p) { memset(p,0,sizeof(*p)); }
int s2_rumble_submit(s2_rumble_player *p,const uint8_t *packet,size_t len,uint16_t ms) {
    s2_rumble decoded;unsigned side;
    int rc=s2_decode_rumble(packet,len,&decoded);
    if(rc!=S2_OK) return rc;
    for(side=0;side<2;++side) {
        if(!decoded.count[side]) continue; /* silence block is no update */
        if(p->valid[side] && p->sequence[side]==decoded.sequence[side] &&
           (uint16_t)(ms-p->started[side])<50u) continue;
        p->count[side]=decoded.count[side];p->sequence[side]=decoded.sequence[side];
        memcpy(p->strength[side],decoded.strength[side],3);
        p->started[side]=ms;p->valid[side]=1;p->hold_ms[side]=50;
    }
    return S2_OK;
}
int s2_rumble_preset(s2_rumble_player *p,uint8_t effect,uint16_t ms) {
    static const uint8_t strength[8]={0,55,40,65,75,45,55,70};
    static const uint16_t duration[8]={0,1000,800,100,150,120,120,200};
    unsigned side;
    if(effect>7) return S2_INVALID;
    s2_rumble_stop(p);
    if(effect) for(side=0;side<2;++side) {
        p->count[side]=1;p->valid[side]=1;p->sequence[side]=255;
        p->strength[side][0]=strength[effect];p->hold_ms[side]=duration[effect];
        p->started[side]=ms;
    }
    return S2_OK;
}
void s2_rumble_output(s2_rumble_player *p,uint16_t ms,uint8_t output[2]) {
    unsigned side;
    for(side=0;side<2;++side) {
        unsigned elapsed=(uint16_t)(ms-p->started[side]),index=elapsed/5u;
        output[side]=0;
        if(!p->valid[side]) continue;
        if(elapsed>=p->hold_ms[side]) { p->valid[side]=0;continue; }
        if(index>=p->count[side]) index=p->count[side]-1u;
        output[side]=p->strength[side][index];
    }
}

/* SPDX-License-Identifier: MIT */
#include "s2_l2cap.h"
#include "s2_protocol.h"
#include <string.h>
int s2_l2cap_feed(s2_l2cap_rx *r,const uint8_t *p,size_t n,const uint8_t **att,size_t *length) {
    unsigned llid;size_t payload;
    *att=0;*length=0;
    if(!p || n<2 || n!=(size_t)p[1]+2u) { r->used=r->expected=0;return S2_INVALID; }
    llid=p[0]&3u;payload=n-2;
    if(llid==2) {
        size_t expected;
        r->used=r->expected=0;
        if(payload<4) return S2_SHORT;
        if(p[4]!=4 || p[5]!=0) return S2_UNSUPPORTED;
        expected=6u+(unsigned)p[2]+((unsigned)p[3]<<8);
        if(expected>S2_RX_CAPACITY) return S2_CAPACITY;
        if(n>expected || expected==6) return S2_INVALID;
        memcpy(r->bytes,p,n);r->used=(uint16_t)n;r->expected=(uint16_t)expected;
    } else if(llid==1) {
        if(!r->expected) return S2_SEQUENCE;
        if(payload>(size_t)(r->expected-r->used)) {
            r->used=r->expected=0;return S2_INVALID;
        }
        memcpy(r->bytes+r->used,p+2,payload);r->used=(uint16_t)(r->used+payload);
    } else return S2_UNSUPPORTED;
    if(r->used!=r->expected) return 0;
    *att=r->bytes+6;*length=r->expected-6u;r->used=r->expected=0;
    return 1;
}

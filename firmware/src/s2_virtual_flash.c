/* SPDX-License-Identifier: MIT */
#include "s2_virtual_flash.h"
/* Common controller setup bytes observed in both stick blocks. These opaque
 * profile defaults are not a donor unit's nine-byte stick calibration. */
static const uint8_t setup[40]={
    1,0xad,0xd9,0x9a,0x55,0x56,0x65,0xa0,0,0x0a,0xa0,0,0x0a,
    0xe2,0x20,0x0e,0xe2,0x20,0x0e,0x9a,0xad,0xd9,0x9a,0xad,0xd9,
    0x0a,0xa5,0x50,0x0a,0xa5,0x50,0x2f,0xf6,0x62,0x2f,0xf6,0x62,0x0a,255,255};
/* Centre, positive travel, negative travel, in packed 12-bit X/Y pairs.
 * Input reports are the original calibrated 16-bit result normalized to 12
 * bits. Giving the host donor calibration would distort this range twice. */
static const uint8_t calibration[9]={0,0x08,0x80,0xff,0xf7,0x7f,0,0x08,0x80};
static uint8_t byte_at(const uint8_t mac[6],uint32_t a) {
    static const char hex[]="0123456789ABCDEF";
    static const uint8_t identity[]={0x7e,5,0x69,0x20,1,6,1,
        0x23,0x23,0x23,0xa0,0xa0,0xa0,0xe6,0xe6,0xe6,0x32,0x32,0x32};
    if(a==0x13000) return 1;
    if(a==0x13001) return 0;
    if(a>=0x13002 && a<0x13012) {
        unsigned i=(unsigned)(a-0x13002);
        if(i<3) return (uint8_t)"2CW"[i];
        if(i==15) return 0;
        i-=3;return (uint8_t)hex[(mac[5-i/2]>>(i&1u?0:4))&15];
    }
    if(a>=0x13012 && a<0x13025) return identity[a-0x13012];
    if((a>=0x13080 && a<0x130b1) || (a>=0x130c0 && a<0x130f1)) {
        unsigned i=(unsigned)(a&63u);return i<40?setup[i]:calibration[i-40];
    }
    /* Absent IMU, NFC and user overrides remain erased, not fabricated data. */
    return 255;
}
int s2_virtual_flash_read(const uint8_t local[6],uint32_t a,uint8_t *out,size_t n) {
    size_t i;
    if(!local || !out || a>0x200000u || n>0x200000u-a) return 0;
    for(i=0;i<n;++i) out[i]=byte_at(local,a+(uint32_t)i);
    return 1;
}

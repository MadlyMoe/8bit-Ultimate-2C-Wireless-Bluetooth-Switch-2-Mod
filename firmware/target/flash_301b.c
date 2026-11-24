/* SPDX-License-Identifier: MIT
 * Exact v1.09 flash status bindings. Stock main enables block protection at
 * a38a; stock writers call bd00 first. Preserve the caller's protection bits
 * instead of leaving the entire chip unlocked as that stock helper does.
 */
#include "runtime_301b.h"

static uint16_t flash_status(uint16_t mask) {
    return mask>255?FN(0x102dcu,uint16_t,(void))():FN(0x1025cu,uint8_t,(void))();
}
static void flash_protection(uint16_t mask,uint16_t bits) {
    /* These stock helpers replace ONLY the supplied mask in status register
     * 1 (10268) or registers 1+2 (102f8), preserving all other status bits. */
    if(mask>255) FN(0x102f8u,void,(uint16_t,uint16_t))(bits,mask);
    else FN(0x10268u,void,(uint8_t,uint8_t))((uint8_t)bits,(uint8_t)mask);
}
int s2_301b_flash_close(uint32_t saved) {
    uint16_t mask=(uint16_t)(saved>>16),bits=(uint16_t)saved;
    if((flash_status(mask)&mask)!=bits) flash_protection(mask,bits);
    return (flash_status(mask)&mask)==bits;
}
int s2_301b_flash_open(uint32_t *saved) {
    uint32_t id=FN(0xff14u,uint32_t,(void))();uint16_t mask,bits;
    /* Same four IDs/masks used by stock bb0c. Unknown chips fail closed.
     * For 136085 stock also clears SR3.WPS; refuse that alternate protection
     * scheme instead of modifying an additional persistent configuration bit. */
    if(id==0x1160c8u || id==0x1360c8u) mask=0x1c;
    else if(id==0x114485u) mask=0x7c;
    else if(id==0x136085u) {
        if(FN(0xff78u,uint8_t,(void))()&4) return 0;
        mask=0x407c;
    } else return 0;
    bits=(uint16_t)(flash_status(mask)&mask);*saved=((uint32_t)mask<<16)|bits;
    if(!bits) return 1;
    flash_protection(mask,0);
    if(!(flash_status(mask)&mask)) return 1;
    s2_301b_flash_close(*saved);return 0;
}

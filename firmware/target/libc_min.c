/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>
void *memcpy(void *out,const void *in,size_t n) {
    unsigned char *d=out;const unsigned char *s=in;while(n--) *d++=*s++;return out;
}
void *memset(void *out,int value,size_t n) {
    unsigned char *d=out;while(n--) *d++=(unsigned char)value;return out;
}
int memcmp(const void *left,const void *right,size_t n) {
    const unsigned char *a=left,*b=right;
    while(n--) { int d=(int)*a++-(int)*b++;if(d) return d; }return 0;
}
static uint32_t divide(uint32_t n,uint32_t d,uint32_t *remainder) {
    uint32_t q=0,bit=1;
    if(!d) { *remainder=n;return UINT32_MAX; }
    while(d<=n/2u && !(d&0x80000000u)) { d<<=1;bit<<=1; }
    while(bit) { if(n>=d) { n-=d;q|=bit; }d>>=1;bit>>=1; }
    *remainder=n;return q;
}
uint32_t __udivsi3(uint32_t n,uint32_t d) { uint32_t r;return divide(n,d,&r); }
uint32_t __umodsi3(uint32_t n,uint32_t d) { uint32_t r;divide(n,d,&r);return r; }

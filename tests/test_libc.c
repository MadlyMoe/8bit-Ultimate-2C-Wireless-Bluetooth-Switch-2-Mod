#include <assert.h>
#include <stdint.h>
#include <stdio.h>
uint32_t __udivsi3(uint32_t,uint32_t);
uint32_t __umodsi3(uint32_t,uint32_t);
int main(void) {
    uint32_t seed=0x12345678;
    for(unsigned i=0;i<100000;++i) {
        seed=seed*1664525u+1013904223u;uint32_t n=seed;
        seed=seed*1664525u+1013904223u;uint32_t d=seed|1;
        assert(__udivsi3(n,d)==n/d && __umodsi3(n,d)==n%d);
        d=1u<<(i%32);assert(__udivsi3(n,d)==n/d && __umodsi3(n,d)==n%d);
    }
    assert(__udivsi3(UINT32_MAX,1)==UINT32_MAX);
    assert(__udivsi3(10,0)==UINT32_MAX && __umodsi3(10,0)==10);
    puts("PASS: bounded TC32 division and remainder against native arithmetic");
}

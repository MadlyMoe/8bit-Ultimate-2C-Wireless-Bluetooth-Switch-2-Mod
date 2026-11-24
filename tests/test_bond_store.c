/* SPDX-License-Identifier: MIT */
#include "s2_bond_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[256]; int budget,erases,read_fail; } flash_model;
static int read_flash(void *ctx,uint32_t a,uint8_t *p,size_t n) {
    flash_model *f=ctx; assert(a+n<=sizeof(f->bytes));
    if(f->read_fail) return 0;
    memcpy(p,f->bytes+a,n); return 1;
}
static int program(void *ctx,uint32_t a,const uint8_t *p,size_t n) {
    flash_model *f=ctx; assert(a+n<=sizeof(f->bytes));
    while(n--) {
        if(!f->budget) return 0;
        if(f->budget>0) --f->budget;
        assert((f->bytes[a]&*p)==*p); f->bytes[a++]&=*p++;
    }
    return 1;
}
static int erase(void *ctx,unsigned sector) {
    flash_model *f=ctx; assert(sector<2); ++f->erases;
    memset(f->bytes+sector*128,255,128); return 1;
}
static s2_saved_bond bond(unsigned tag) {
    s2_saved_bond b; memset(&b,(int)tag,sizeof(b)); b.peer_type=0; return b;
}
int main(void) {
    flash_model f,baseline; s2_bond_store s,reboot;
    s2_bond_io io={read_flash,program,erase,&f,128};
    s2_saved_bond a=bond(1),b=bond(2),got;
    memset(&f,0,sizeof(f)); memset(f.bytes,255,sizeof(f.bytes)); f.budget=-1;
    assert(s2_bond_open(&s,&io,&got)==S2_STORE_EMPTY);
    assert(s2_bond_save(&s,&a)==0); baseline=f;
    /* Power loss at EVERY programmed byte, including the commit word. */
    for(int cut=0;cut<=48;++cut) {
        f=baseline; f.budget=cut;
        assert(s2_bond_open(&s,&io,&got)==0);
        int rc=s2_bond_save(&s,&b);
        assert(rc==(cut<48?S2_STORE_IO:S2_OK));
        assert(!f.erases && !memcmp(f.bytes,baseline.bytes,64));
        assert(s2_bond_open(&reboot,&io,&got)==0);
        assert(!memcmp(&got,cut<48?&a:&b,sizeof(got)));
    }
    f=baseline;
    assert(s2_bond_open(&s,&io,&got)==0);
    for(unsigned i=2;i<=4;++i) { b=bond(i); assert(s2_bond_save(&s,&b)==0); }
    assert(s2_bond_save(&s,&a)==S2_STORE_FULL && !f.erases);
    assert(s2_bond_prepare(&s)==0 && f.erases==1);
    assert(s2_bond_open(&reboot,&io,&got)==0 && !memcmp(&got,&b,sizeof(got)));
    assert(s2_bond_save(&s,&a)==0);
    assert(s2_bond_open(&reboot,&io,&got)==0 && !memcmp(&got,&a,sizeof(got)));
    assert(s2_bond_for_peer(&got,a.peer_type,a.peer_le));
    assert(!s2_bond_for_peer(&got,1,a.peer_le));
    assert(!s2_bond_for_peer(&got,0,b.peer_le));
    /* Latest record corruption rolls back to a committed earlier record. */
    f.bytes[s.latest+18]^=1;
    assert(s2_bond_open(&reboot,&io,&got)==0 && !memcmp(&got,&b,sizeof(got)));
    f.read_fail=1;
    assert(s2_bond_open(&reboot,&io,&got)==S2_STORE_IO && !reboot.io);
    /* Diagnostic power loss must never alter the existing bond or erase a
     * sector. Two remaining slots stay available for pairing plus refresh. */
    { uint8_t payload[32]={2,3,255};
      for(int cut=0;cut<=48;++cut) {
        f=baseline;f.budget=cut;
        assert(s2_bond_trace_append(&io,payload)==(cut<48?S2_STORE_IO:S2_OK));
        assert(!f.erases && !memcmp(f.bytes,baseline.bytes,64));
        assert(s2_bond_open(&reboot,&io,&got)==S2_OK && !memcmp(&got,&a,sizeof(got)));
      }
      f=baseline;
      assert(s2_bond_trace_append(&io,payload)==S2_OK);
      assert(!memcmp(f.bytes+64,"S2DG\1\0",6) && !memcmp(f.bytes+72,payload,32));
      assert(!memcmp(f.bytes+108,"LOGD",4));
      assert(s2_bond_trace_append(&io,payload)==S2_STORE_FULL && !f.erases);
      assert(s2_bond_open(&s,&io,&got)==S2_OK);
      assert(s2_bond_save(&s,&b)==S2_OK && s2_bond_save(&s,&a)==S2_OK && !f.erases);
    }
    puts("PASS: bond storage survives every partial write; reclamation preserves newest bond; peer binding and corruption recovery.");
    return 0;
}

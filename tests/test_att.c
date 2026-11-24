/* SPDX-License-Identifier: MIT */
#include "s2_gatt.h"
#include "s2_l2cap.h"
#include "s2_transport.h"
#include "s2_virtual_flash.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct { uint8_t sent[128];size_t length;int busy,writes; } mock;
static int read_value(void *ctx,uint16_t h,uint16_t off,uint8_t *p,size_t cap,size_t *len) {
    (void)ctx;*len=h==0xe?63:2;
    if(off>*len) return 7;
    if(cap>*len-off) cap=*len-off;
    for(size_t i=0;i<cap;++i) p[i]=(uint8_t)(off+i);return 0;
}
static int write_value(void *ctx,uint16_t h,const uint8_t *p,size_t n) {
    mock *m=ctx;(void)p;
    if(h!=0x14 || n!=8) return 13;
    ++m->writes;return 0;
}
static int send_value(void *ctx,const uint8_t *p,size_t n) {
    mock *m=ctx;if(m->busy) return S2_BUSY;
    assert(n<=sizeof(m->sent));memcpy(m->sent,p,n);m->length=n;return 0;
}
static const s2_att_io io={read_value,write_value,send_value};
static void command(s2_att_state *s,mock *m,const uint8_t *p,size_t n) {
    assert(s2_att_receive(s,&io,m,p,n)==0);assert(s2_att_poll(s,&io,m)==0);
}
static void test_att(void) {
    struct { uint32_t first;s2_att_state state;uint32_t last; } g={0x12345678,{0},0xabcdef01};
    s2_att_state *s=&g.state;mock m={0};uint8_t p[24]={0};
    s2_att_reset(s);
    p[0]=2;p[1]=0;p[2]=2;command(s,&m,p,3);
    assert(s->mtu==512 && m.length==3 && m.sent[0]==3 && m.sent[1]==0 && m.sent[2]==2);
    p[0]=0x10;p[1]=1;p[2]=0;p[3]=255;p[4]=255;p[5]=0;p[6]=0x28;
    command(s,&m,p,7);assert(m.length==22 && m.sent[1]==20 && m.sent[2]==1 && m.sent[4]==7);
    p[1]=8;command(s,&m,p,7);assert(m.sent[2]==8 && m.sent[4]==0x32);
    p[1]=0x33;command(s,&m,p,7);assert(m.length==14 && m.sent[2]==0x33 && m.sent[8]==0x38 && m.sent[10]==0x3b);
    p[0]=8;p[1]=9;p[3]=0x32;p[4]=0;p[5]=3;
    command(s,&m,p,7);assert(m.length==23 && m.sent[0]==9 && m.sent[1]==21);
    assert(m.sent[2]==9 && m.sent[4]==0x12 && m.sent[5]==0xa && m.sent[7]==0xd2);
    p[0]=4;p[1]=0xa;command(s,&m,p,5);assert(m.length==20 && m.sent[0]==5 && m.sent[1]==2);
    p[0]=0xa;p[1]=0xe;command(s,&m,p,3);assert(m.length==64 && m.sent[1]==0 && m.sent[63]==62);
    p[0]=0xc;p[3]=60;p[4]=0;command(s,&m,p,5);assert(m.length==4 && m.sent[1]==60);
    p[3]=64;command(s,&m,p,5);assert(m.length==5 && m.sent[0]==1 && m.sent[4]==7);
    memset(p,0,sizeof(p));p[0]=0x12;p[1]=0x14;m.busy=1;
    assert(s2_att_receive(s,&io,&m,p,11)==0 && m.writes==1);
    assert(s2_att_poll(s,&io,&m)==S2_BUSY && m.writes==1);
    assert(s2_att_receive(s,&io,&m,p,11)==S2_BUSY && m.writes==1);
    p[0]=0x52;assert(s2_att_receive(s,&io,&m,p,11)==S2_OK && m.writes==2 && s->pending);
    p[1]=0x35;assert(s2_att_receive(s,&io,&m,p,11)==S2_OK && s->pending);
    p[1]=255;assert(s2_att_receive(s,&io,&m,p,11)==S2_OK && s->pending);
    assert(s2_att_receive(s,&io,&m,p,1)==S2_OK && s->pending);
    s->mtu=23;assert(s2_att_receive(s,&io,&m,p,sizeof(p))==S2_OK && s->pending);
    m.busy=0;assert(s2_att_poll(s,&io,&m)==0 && m.sent[0]==0x13 && m.writes==2);
    p[0]=0x12;p[1]=0x35;command(s,&m,p,11);assert(m.sent[4]==3 && m.writes==2);
    /* Malformed opcode/length combinations must leave the small state bounded. */
    for(unsigned op=0;op<256;++op) for(size_t n=1;n<24;++n) {
        s2_att_reset(s);memset(p,0xff,sizeof(p));p[0]=(uint8_t)op;
        (void)s2_att_receive(s,&io,&m,p,n);(void)s2_att_poll(s,&io,&m);
        assert(g.first==0x12345678 && g.last==0xabcdef01);
    }
}
static void test_reassembly(void) {
    struct { uint32_t a;s2_l2cap_rx r;uint32_t b; } guarded={1,{0},2};
    uint8_t input[80],f[29];const uint8_t *p;size_t n;
    for(unsigned i=0;i<80;++i) input[i]=(uint8_t)i;
    f[0]=2;f[1]=27;f[2]=80;f[3]=0;f[4]=4;f[5]=0;memcpy(f+6,input,23);
    assert(s2_l2cap_feed(&guarded.r,f,29,&p,&n)==0 && !p);
    f[0]=1;memcpy(f+2,input+23,27);
    assert(s2_l2cap_feed(&guarded.r,f,29,&p,&n)==0);
    memcpy(f+2,input+50,27);assert(s2_l2cap_feed(&guarded.r,f,29,&p,&n)==0);
    f[1]=3;memcpy(f+2,input+77,3);assert(s2_l2cap_feed(&guarded.r,f,5,&p,&n)==1);
    assert(n==80 && !memcmp(p,input,80));
    f[0]=2;f[1]=27;f[2]=255;f[3]=255;f[4]=4;f[5]=0;
    assert(s2_l2cap_feed(&guarded.r,f,29,&p,&n)==S2_CAPACITY);
    f[0]=1;assert(s2_l2cap_feed(&guarded.r,f,29,&p,&n)==S2_SEQUENCE);
    assert(guarded.a==1 && guarded.b==2);
}
static unsigned unpack(const uint8_t *p,unsigned index) {
    unsigned a=index*12u/8u,b=index*12u%8u;
    return (((unsigned)p[a]|((unsigned)p[a+1]<<8))>>b)&4095u;
}
static void test_virtual(void) {
    uint8_t mac[6]={1,2,3,4,5,6},out[79];
    assert(s2_virtual_flash_read(mac,0x13000,out,64));
    assert(!memcmp(out+2,"2CW060504030201",15) && out[17]==0 && out[18]==0x7e);
    assert(s2_virtual_flash_read(mac,0x130a8,out,9));
    assert(unpack(out,0)==2048 && unpack(out,1)==2048 && unpack(out,2)==2047 && unpack(out,4)==2048);
    assert(s2_virtual_flash_read(mac,0x1fc040,out,79));
    for(unsigned i=0;i<79;++i) assert(out[i]==255);
    assert(!s2_virtual_flash_read(mac,0xffffffffu,out,1));
    assert(!s2_virtual_flash_read(mac,0x1fffffu,out,2));
}
int main(void) {
    test_att();test_reassembly();test_virtual();
    puts("PASS: full GATT discovery, MTU negotiation, long reads, retry-safe writes, bounded fragment assembly and unit-specific normalized calibration");return 0;
}

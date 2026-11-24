/* SPDX-License-Identifier: MIT */
#include "s2_protocol.h"
#include "aes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t a1[16]={0x35,3,0xe9,0x29,0x82,0x87,0x71,0x24,0xbe,0xa8,0x0c,0x66,0x46,0x15,0x83,0x4b};
static const uint8_t a2[16]={0x6f,0xc6,0xdf,0x8a,0xd8,0xfe,0xdf,0x15,0xbb,0x8c,0x15,0xe9,0x1f,0x32,5,0x44};
static const uint8_t ltk[16]={0x69,0xf5,7,0x50,0xae,0x58,0x74,0xc5,4,0x83,0x6f,0x43,0x82,0x0f,0xdc,0x5b};
static const uint8_t proof[16]={0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
static const uint8_t local[6]={1,2,3,4,5,6};
typedef struct { int committed,fail; s2_bond bond; } mock;
static int commit(void *ctx,const s2_bond *bond) {
    mock *m=(mock *)ctx;
    if(m->fail) return 0;
    m->bond=*bond; ++m->committed; return 1;
}
static int flash(void *ctx,uint32_t addr,uint8_t *out,size_t n) {
    (void)ctx;
    if(addr!=0x13000) return 0;
    memset(out,0x5a,n); return 1;
}
static int issue(s2_session *s,int combined,uint8_t cmd,uint8_t sub,
                 const uint8_t *p,size_t n,uint8_t *out,size_t *written) {
    uint8_t packet[200]={0}; size_t off=combined?33u:0u;
    assert(n<=sizeof(packet)-off-8);
    if(combined) memset(packet,0xcc,33); /* real rumble prefix is not always zero */
    packet[off]=cmd; packet[off+1]=0x91; packet[off+2]=1; packet[off+3]=sub;
    packet[off+5]=(uint8_t)n;
    if(n) memcpy(packet+off+8,p,n);
    return s2_command(s,combined,packet,off+8+n,out,S2_MAX_REPLY,written);
}
static void test_crypto(void) {
    /* NIST AES-128 known-answer test, independent of the Nintendo vector. */
    const uint8_t key[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint8_t block[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const uint8_t expected[16]={0x69,0xc4,0xe0,0xd8,0x6a,0x7b,4,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    struct AES_ctx aes; uint8_t got[16];
    AES_init_ctx(&aes,key); AES_ECB_encrypt(&aes,block);
    assert(memcmp(block,expected,16)==0);
    s2_derive_ltk(a1,got); assert(memcmp(got,ltk,16)==0);
    s2_confirm(got,a2,block); assert(memcmp(block,proof,16)==0);
}
static void test_pairing(void) {
    int combined;
    for(combined=0;combined<=1;++combined) {
        s2_session s; mock m={0}; uint8_t out[S2_MAX_REPLY],p[17]={0}; size_t n,off=combined?14u:0u;
        const uint8_t address[14]={0,2,10,20,30,40,50,60,11,20,30,40,50,60};
        s2_init(&s,local,commit,flash,&m);
        assert(issue(&s,combined,0x15,3,p,1,out,&n)==S2_SEQUENCE && n==0 && !m.committed);
        assert(issue(&s,combined,0x15,1,address,sizeof(address),out,&n)==0);
        assert(n==off+17 && memcmp(out+off+11,local,6)==0);
        assert(out[off]==0x15 && out[off+1]==1 && out[off+4]==0x10 && out[off+5]==0x78);
        memcpy(p+1,a1,16);
        assert(issue(&s,combined,0x15,4,p,17,out,&n)==0 && n==off+25);
        memcpy(p+1,a2,16);
        assert(issue(&s,combined,0x15,2,p,17,out,&n)==0);
        assert(memcmp(out+off+9,proof,16)==0);
        m.fail=1;
        assert(issue(&s,combined,0x15,3,p,1,out,&n)==S2_BACKEND && n==0 && !m.committed);
        m.fail=0;
        assert(issue(&s,combined,0x15,3,p,1,out,&n)==0 && n==off+9 && m.committed==1);
        assert(memcmp(m.bond.host_le,address+2,6)==0 && memcmp(m.bond.ltk_wire,ltk,16)==0);
        s2_disconnect(&s); assert(s.stage==0 && m.committed==1);
        assert(issue(&s,combined,0x15,2,p,17,out,&n)==S2_SEQUENCE && n==0);
    }
}
static void test_reports(void) {
    uint8_t out[S2_REPORT_SIZE]; unsigned x;
    s2_input in={{0},2048,2048,2048,2048,100,0,0};
    s2_report(out,&in,255);
    assert(out[0]==255 && out[1]==36 && out[11]==0x30);
    assert(memcmp(out+5,"\x00\x08\x80\x00\x08\x80",6)==0);
    for(x=12;x<63;++x) assert(out[x]==0); /* no fabricated sensors */
    for(x=0;x<4096;++x) {
        unsigned dx,dy;
        in.lx=(uint16_t)x; in.ly=(uint16_t)(4095-x);
        s2_report(out,&in,0);
        dx=(unsigned)out[5]+256u*(out[6]&15u);
        dy=(unsigned)(out[6]>>4)+16u*out[7];
        assert(dx==x && dy==4095-x);
    }
    in.lx=65535;in.ly=65535;in.battery_percent=255;in.charging=1;in.external_power=1;
    in.buttons[0]=255;in.buttons[1]=255;in.buttons[2]=255;
    s2_report(out,&in,0);
    assert(out[5]==255 && out[6]==255 && out[7]==255 && out[1]==39);
    assert(out[2]==255 && out[3]==255 && out[4]==15);
}
static void test_advertisements(void) {
    uint8_t a[31]; s2_bond bond={{10,20,30,40,50,60},{0}};
    const uint8_t expected[31]={2,1,6,27,255,0x53,5,1,0,3,0x7e,5,0x69,0x20,0,1,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0};
    assert(s2_advertisement(a,0,0)==0 && memcmp(a,expected,31)==0);
    assert(s2_advertisement(a,0,1)==S2_INVALID);
    assert(s2_advertisement(a,&bond,0)==0 && memcmp(a+17,bond.host_le,6)==0 && a[16]==0);
    assert(s2_advertisement(a,&bond,1)==0 && a[16]==0x81);
}
static void test_bounds(void) {
    s2_session s; mock m={0}; uint8_t input[256]={0},out[S2_MAX_REPLY+2]; size_t n,len;
    s2_init(&s,local,commit,flash,&m);
    for(len=0;len<41;++len)
        assert(s2_command(&s,1,input,len,out+1,S2_MAX_REPLY,&n)==S2_SHORT && n==0);
    input[0]=0x15;input[1]=0x91;input[3]=1;input[9]=1;
    for(len=0;len<37;++len) {
        assert(s2_command(&s,0,input,16,out+1,len,&n)==S2_CAPACITY);
        assert(s.stage==0 && n==0);
    }
    /* Every byte-sized command/subcommand under lengths 8..63: guard bytes and
     * output bounds must survive malformed traffic, without accepting a bond. */
    for(unsigned cmd=0;cmd<256;++cmd) for(len=8;len<=63;++len) {
        memset(input,0,sizeof(input)); input[0]=(uint8_t)cmd;input[1]=0x91;input[3]=(uint8_t)len;
        memset(out,0xa5,sizeof(out));
        (void)s2_command(&s,(int)(cmd&1u),input,len,out+1,S2_MAX_REPLY,&n);
        assert(out[0]==0xa5 && out[S2_MAX_REPLY+1]==0xa5 && n<=S2_MAX_REPLY && !m.committed);
    }
    { uint8_t p[8]={S2_MAX_FLASH_READ,0x7e,0,0,0,0x30,1,0};
      assert(issue(&s,1,2,4,p,8,out,&n)==0 && n==S2_MAX_REPLY && out[S2_MAX_REPLY-1]==0x5a);
      p[0]=S2_MAX_FLASH_READ+1;assert(issue(&s,1,2,4,p,8,out,&n)==S2_INVALID && n==0);
      p[0]=S2_MAX_FLASH_READ;memset(p+4,255,4);assert(issue(&s,1,2,4,p,8,out,&n)==S2_INVALID); }
    { uint8_t f=255;
      assert(issue(&s,0,0x0c,2,&f,1,out,&n)==0 && s.features==3);
      assert(issue(&s,0,0x0c,1,&f,1,out,&n)==0 && n==20);
      assert(out[12]==7 && out[13]==7 && out[14]==0 && out[17]==0); }
    assert(issue(&s,0,0x0d,1,0,0,out,&n)==S2_UNSUPPORTED && n==0);
}
static void test_initialization(void) {
    s2_session s; mock m={0}; uint8_t out[S2_MAX_REPLY],packet[24]={0},flags; size_t n;
    s2_init(&s,local,commit,flash,&m);
    for(int combined=0;combined<=1;++combined) {
        size_t off=combined?14u:0u;
        assert(issue(&s,combined,0x11,3,0,0,out,&n)==0 && n==off+37);
        assert(memcmp(out+off+8,"\x01\x20\x03\x00\x00\x0a\xe8",7)==0);
        assert(out[n-1]==0x41);
        assert(issue(&s,combined,0x18,1,0,0,out,&n)==0 && n==off+16);
        assert(memcmp(out+off+8,"\x00\x00\x40\xf0\x00\x00\x60\x00",8)==0);
        assert(issue(&s,combined,0x18,2,0,0,out,&n)==S2_UNSUPPORTED && n==0);
        for(unsigned i=0;i<256;++i) {
            flags=(uint8_t)i;
            assert(issue(&s,combined,0x0c,1,&flags,1,out,&n)==0 && n==off+20);
            assert(out[off+12]==((flags&1)?7:0) && out[off+13]==((flags&2)?7:0));
            for(size_t j=off+14;j<off+20;++j) assert(out[j]==0);
        }
    }
    packet[0]=0x15;packet[1]=0x91;packet[3]=1;packet[5]=8;
    packet[9]=1; /* A truncated host address may not consume the rest of the buffer. */
    assert(s2_command(&s,0,packet,15,out,sizeof(out),&n)==S2_SHORT && s.stage==0);
    packet[5]=0;
    assert(s2_command(&s,0,packet,sizeof(packet),out,sizeof(out),&n)==S2_SHORT && s.stage==0);
    packet[0]=0x0c;packet[3]=2;packet[5]=1;packet[8]=1;
    assert(s2_command(&s,0,packet,sizeof(packet),out,sizeof(out),&n)==0 && s.features==1);
}
int main(void) {
    test_crypto();test_pairing();test_reports();test_advertisements();test_bounds();test_initialization();
    printf("PASS: AES + pairing, 2 channels, persistence failure, 4096 axes, advertising, 512 capability queries, late-init probes, framing and 14336 malformed packets; session=%u bytes\n",(unsigned)sizeof(s2_session));
    return 0;
}

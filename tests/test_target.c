/* SPDX-License-Identifier: MIT
 * Executes the real target adapter against a simulated stock ABI. This proves
 * software ordering and bounds, NOT radio compatibility or hardware addresses.
 */
#include "runtime_301b.h"
#include "s2_stock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t s2_test_registers[0x45000];
static uint8_t flash[0x2000],ad[31],sent[128],motors[2];
static unsigned sent_size,programs,trace_programs,erases,disconnects,tx_busy,led_state;
static unsigned radio_polls;
static int fifo_result;
static uint32_t flash_id=0x1360c8;
static uint16_t flash_sr=0x3c;
static uint8_t flash_sr3;
static unsigned protection_writes,fail_unlock,fail_restore;
static int (*receive_cb)(uint16_t,const uint8_t *);
static int (*ltk_cb)(uint16_t,const uint8_t *,uint16_t);
static void (*connect_cb)(unsigned,const uint8_t *,int);
static void (*disconnect_cb)(unsigned,const uint8_t *,int);
static void noop(void) {}
static void no_pointer(const uint8_t *p) { (void)p; }
static void no_unsigned(unsigned p) { (void)p; }
static void no_byte(uint8_t p) { (void)p; }
static void no_two(unsigned p,unsigned q) { (void)p;(void)q; }
static int check_fifos(void) { return fifo_result; }
static void poll_radio(void) { ++radio_polls; }
static uint32_t flash_jedec(void) { return flash_id; }
static uint8_t read_sr1(void) { return (uint8_t)flash_sr; }
static uint16_t read_sr12(void) { return flash_sr; }
static uint8_t read_sr3(void) { return flash_sr3; }
static void set_protection(uint16_t bits,uint16_t mask) {
    ++protection_writes;
    if((!bits && fail_unlock) || (bits && fail_restore)) return;
    flash_sr=(uint16_t)((flash_sr&~mask)|(bits&mask));
}
static void set_protection8(uint8_t bits,uint8_t mask) { set_protection(bits,mask); }
static uint16_t protection_mask(void) { return flash_id==0x136085?0x407c:flash_id==0x114485?0x7c:0x1c; }
static void flash_read(uint32_t a,uint32_t n,uint8_t *p) {
    assert(a>=0x3d000 && a<=0x3f000 && n<=0x3f000-a);memcpy(p,flash+a-0x3d000,n);
}
static void flash_write(uint32_t a,uint32_t n,const uint8_t *p) {
    assert(a>=0x3d000 && a<0x3f000 && n<=0x3f000-a);
    /* Real NOR silently ignores protected writes. The previous adapter did
     * not unlock it, and therefore never produced even its boot record. */
    if(flash_sr&protection_mask()) return;
    if((n==44 && !memcmp(p,"S2DG",4)) || (n==4 && !memcmp(p,"LOGD",4))) {
        assert(!s2_link.transport.connected && R8(0x843a32)==0);++trace_programs;
    } else ++programs;
    for(unsigned i=0;i<n;++i) { assert((flash[a-0x3d000+i]&p[i])==p[i]);flash[a-0x3d000+i]&=p[i]; }
}
static void erase(uint32_t a) { assert(a==0x3d000 || a==0x3e000);assert(R8(0x843a32)==0);
    if(flash_sr&protection_mask()) return;
    ++erases;memset(flash+a-0x3d000,255,4096); }
static int adv_enable(int on) { R8(0x843a32)=(uint8_t)(on?1:0);return 0; }
static int set_ad(const uint8_t *p,unsigned n) { assert(n==31);memcpy(ad,p,n);return 0; }
static int set_scan(const uint8_t *p,unsigned n) { assert(n==20 && p[0]==19);return 0; }
static int adv_params(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,const uint8_t *f,unsigned g,unsigned h) {
    assert(a==48 && b==56 && !c && !d && !e && !f && g==7 && !h);return 0;
}
static void fifo(void *p,unsigned size,unsigned count) {
    assert((p==(void *)0x843178 && size==40 && count==8) || (p==(void *)0x8432b8 && size==64 && count==4));
}
static void register_rx(void *f) { receive_cb=(int (*)(uint16_t,const uint8_t *))f; }
static void register_ltk(void *f) { ltk_cb=(int (*)(uint16_t,const uint8_t *,uint16_t))f; }
static void register_event(unsigned event,void *f) {
    if(event==3) connect_cb=(void (*)(unsigned,const uint8_t *,int))f;
    else if(event==4) disconnect_cb=(void (*)(unsigned,const uint8_t *,int))f;
    else assert(event==15);
}
static int enqueue(uint16_t connection,uint16_t cid,const uint8_t *h,int hn,const uint8_t *p,int n) {
    assert(connection==0x8040 && cid==4 && hn>0 && n>=0 && hn+n<=128);
    if(tx_busy) return 0x81;
    memcpy(sent,h,(size_t)hn);memcpy(sent+hn,p,(size_t)n);sent_size=(unsigned)(hn+n);return 0;
}
static int terminate(uint8_t reason) {
    assert(reason==0x13);
    if(tx_busy) return 0x3a;
    ++disconnects;R8(0x843a32)=0;R8(0x8435f2)=0;
    disconnect_cb(4,&reason,1);return 0;
}
static int enqueue_control(uint16_t connection,const uint8_t *p) {
    assert(connection==0x8040 && !memcmp(p,"\x03\x03\x17\x03\x03",5));
    return tx_busy?0:1;
}
static void set_led(uint8_t id,uint8_t state) {
    assert(id==4 && state<=3);led_state=state;R8(0x843457)=state;
}
static int reject_ltk(uint16_t c) { assert(c==0x40);return 0; }
void *s2_test_function(uintptr_t a) {
    switch(a&~(uintptr_t)1) {
    case 0xfe40:return (void *)flash_read;case 0xfe5c:case 0x10f5c:return (void *)flash_write;case 0xfe98:return (void *)erase;
    case 0xff14:return (void *)flash_jedec;case 0x1025c:return (void *)read_sr1;
    case 0x102dc:return (void *)read_sr12;case 0xff78:return (void *)read_sr3;
    case 0x10268:return (void *)set_protection8;case 0x102f8:return (void *)set_protection;
    case 0x11598:return (void *)adv_enable;case 0x11614:return (void *)set_ad;
    case 0x11690:return (void *)set_scan;case 0x116c0:return (void *)adv_params;
    case 0x11e64:case 0x11ea4:return (void *)fifo;
    case 0x14780:return (void *)register_rx;case 0x121bc:return (void *)register_ltk;
    case 0x110d4:return (void *)register_event;case 0x1484c:return (void *)enqueue;
    case 0x122fc:return (void *)enqueue_control;
    case 0x12438:return (void *)terminate;case 0xd3d8:return (void *)set_led;case 0x13114:return (void *)reject_ltk;
    case 0x11450:return (void *)check_fifos;
    case 0x11188:case 0x12cf8:return (void *)no_pointer;
    case 0x13478:return (void *)no_unsigned;case 0x10994:return (void *)no_byte;
    case 0x11f40:return (void *)no_two;
    case 0x111e8:case 0x114f8:case 0x121a0:return (void *)noop;
    case 0x11220:return (void *)poll_radio;
    case 0xb388:case 0xb14c:return (void *)noop;
    default:fprintf(stderr,"Unmodeled target call %lx\n",(unsigned long)a);abort();
    }
}
void s2_stock_motor(uint8_t l,uint8_t r) { motors[0]=l;motors[1]=r; }
void s2_stock_motor_stop(void) { s2_stock_motor(0,0); }
static uint32_t fixture_buttons=1;
static uint16_t fixture_axes[4]={32767,32767,32767,32767};
void s2_stock_read_calibrated(s2_calibrated_301b *s) {
    memset(s,0,sizeof(*s));memcpy(s->axes,fixture_axes,sizeof(fixture_axes));s->buttons=fixture_buttons;
}
uint16_t s2_stock_battery_mv(void) { return 3900; }
uint8_t s2_stock_battery_percent(void) { return 70; }
int s2_stock_ltk_reply(uint16_t c,const s2_saved_bond *b,uint8_t type,const uint8_t p[6],const uint8_t random[8],uint16_t ediv) {
    assert(c==0x40 && !ediv && !random[0]);assert(s2_bond_for_peer(b,type,p));return S2_OK;
}
static void feed(const uint8_t *att,size_t n) {
    uint8_t ll[29]={2,0,0,0,4,0};size_t used=0,chunk=n>23?23:n;
    ll[1]=(uint8_t)(chunk+4);ll[2]=(uint8_t)n;ll[3]=(uint8_t)(n>>8);
    memcpy(ll+6,att,chunk);receive_cb(0x40,ll);used=chunk;
    while(used<n) { chunk=n-used;if(chunk>27) chunk=27;ll[0]=1;ll[1]=(uint8_t)chunk;memcpy(ll+2,att+used,chunk);receive_cb(0x40,ll);used+=chunk; }
}
static void write_value(uint16_t h,const uint8_t *p,size_t n) {
    uint8_t att[96]={0x52,(uint8_t)h,(uint8_t)(h>>8)};assert(n<=sizeof(att)-3);memcpy(att+3,p,n);feed(att,n+3);
}
static void command(uint8_t cmd,uint8_t sub,const uint8_t *p,size_t n) {
    uint8_t bytes[80]={cmd,0x91,1,sub,0,(uint8_t)n,0,0};if(n) memcpy(bytes+8,p,n);write_value(0x14,bytes,n+8);
}
static void tick(unsigned ms) { R32(0x800740)+=ms*16000u;s2_301b_loop(); }
static void connect_peer(void) {
    assert(R8(0x843a32)==1);
    uint8_t dma[40]={0};dma[4]=0x65;for(unsigned i=0;i<6;++i) dma[6+i]=(uint8_t)(0xa0+i);
    s2_301b_connect_packet(dma);R16(0x843b26)=0x40;R8(0x843a32)=8;connect_cb(3,0,0);
}
static void console_command(uint8_t cmd,uint8_t sub,const uint8_t *p,size_t n) {
    uint8_t bytes[96]={0};
    bytes[33]=cmd;bytes[34]=0x91;bytes[35]=1;bytes[36]=sub;bytes[38]=(uint8_t)n;
    if(n) memcpy(bytes+41,p,n);
    write_value(0x16,bytes,n+41);tick(1);
    assert(!s2_link.failed && !disconnects && !s2_link.transport.count);
    assert(sent[0]==0x1b && sent[1]==0x1e && sent[17]==cmd && sent[20]==sub && sent[22]==0x78);
}
static void console_startup(void) {
    /* Captured Pro2 initialization order from ndeadly/switch2_controller_research,
     * bluetooth_interface.md. Its table omits the 33-byte vibration prefix.
     * Synthetic key material avoids embedding a real host key in this test. */
    uint8_t setup[]={0x12,5,0,1,0},enable[]={1,0},mtu[]={3,0,2};
    uint8_t read[]={64,0x7e,0,0,0,0x30,1,0};
    uint8_t addresses[]={0,2,1,2,3,4,5,6,7,8,9,10,11,12},key[17]={0};
    uint8_t effect[]={3,0,0,0},leds[]={1,0,0,0,0,0,0,0},features[]={0x2f,0,0,0};
    uint8_t vendor_rumble[]={1,255,255,255,255,255,255,255,255,0x35,0,0x46,0,0,0,0,0,0,0,0};
    static const uint32_t reads[]={0x13080,0x130c0,0x1fc040,0x13040,0x13100,0x13060};
    static const uint8_t sizes[]={64,64,64,16,24,32};
    memset(s2_test_registers,0,sizeof(s2_test_registers));memset(flash,255,sizeof(flash));
    programs=erases=disconnects=tx_busy=0;
    R32(0x842e50)=0xfe41;R32(0x842e3c)=0xfe5d;
    memcpy(B80_PTR(0x84386c),"ABCDEF",6);s2_301b_init();
    assert(led_state==3 && !R8(0x843a32));
    tick(1);assert(R8(0x843a32)==1 && flash[64+8]==5 && flash[64+39]==3);
    set_led(4,1);tick(3001);assert(led_state==3);
    tick(1);assert(flash[128+8]==4 && flash[128+38]==1 && flash[128+39]==3);
    connect_peer();tick(1);feed(mtu,sizeof(mtu));
    feed(setup,sizeof(setup));tick(1);
    assert(sent_size==1 && sent[0]==0x13 && !s2_link.failed);
    write_value(0x1f,enable,2);
    console_command(7,1,0,0);console_command(2,4,read,sizeof(read));console_command(0x16,1,0,0);
    console_command(0x15,1,addresses,sizeof(addresses));
    for(unsigned i=1;i<17;++i) key[i]=(uint8_t)i;
    console_command(0x15,4,key,sizeof(key));console_command(0x15,2,key,sizeof(key));console_command(0x15,3,key,1);
    assert(s2_unit.protocol.stage==4 && programs==2);
    assert(flash_sr==0x3c); /* Actual writes completed, entry protection restored. */
    write_value(0x1b,enable,2); /* Normal as well as combined command replies. */
    command(0x0a,2,effect,sizeof(effect));tick(1);
    assert(!motors[0] && !motors[1] && !s2_link.failed);
    R8(0x8435f2)=1;
    effect[0]=2; /* Search by Controller Rumble, before gameplay streaming. */
    console_command(0x0a,2,effect,sizeof(effect));
    assert(motors[0]==40 && motors[1]==40);
    tick(800);assert(!motors[0] && !motors[1]);
    console_command(0x0a,2,effect,sizeof(effect));assert(motors[0]==40 && motors[1]==40);
    effect[0]=0;console_command(0x0a,2,effect,sizeof(effect));assert(!motors[0] && !motors[1]);
    console_command(9,7,leds,sizeof(leds));
    /* The first pattern octet carries the player mask. Exercise a compact
     * frame and a nominal-size header with the reserved trailing bytes absent. */
    console_command(9,7,leds,1);
    uint8_t trimmed_led[33+8+1]={0};
    memcpy(trimmed_led+33,"\x09\x91\x01\x07\x00\x08\x00\x00\x03",9);
    write_value(0x16,trimmed_led,sizeof(trimmed_led));tick(1);
    assert(!s2_link.failed && s2_unit.protocol.led_mask==3 && sent[17]==9 && sent[22]==0x78);
    assert(s2_link.led_lengths==0x81);
    write_value(0x16,trimmed_led,sizeof(trimmed_led)-1);tick(1);
    assert(!s2_link.failed && s2_link.transport.encrypted && s2_unit.protocol.led_mask==3 &&
        !s2_link.transport.count && s2_link.led_lengths==0x80);
    assert(led_state==2); /* Still awaiting input subscription. */
    console_command(0x0c,2,features,sizeof(features));
    for(unsigned i=0;i<6;++i) {
        if(i==5) console_command(0x11,3,0,0);
        read[0]=sizes[i];for(unsigned b=0;b<4;++b) read[4+b]=(uint8_t)(reads[i]>>(8*b));
        console_command(2,4,read,sizeof(read));
    }
    console_command(0x0a,8,vendor_rumble,sizeof(vendor_rumble));
    console_command(0x0c,4,features,sizeof(features));
    { uint8_t rate[]={0x85,0};write_value(0x10,rate,2); }
    write_value(0x0f,enable,2);tick(6);
    assert(!s2_link.failed && !disconnects && sent_size==66 && sent[1]==0x0e);
    assert(led_state==1);
    command(9,8,enable,2);tick(1);
    assert(led_state==1 && s2_unit.protocol.led_flashing);
    command(9,6,0,0);tick(1);
    assert(led_state==1 && !s2_unit.protocol.led_mask);
    console_command(1,0x0c,0,0);
    assert(sent_size==29 && !memcmp(sent+25,"\x61\x12\x50\x0d",4));
    fixture_buttons=1u<<9; /* stock D-pad Up */
    command(0xff,1,0,0);tick(6);
    assert(!s2_link.failed && !disconnects && sent_size==66 && sent[1]==0x0e && sent[6]==8);
    /* Backpressure preserves earlier responses and cannot execute the third
     * command's side effect. Release the FIFO and check both reply handles. */
    tx_busy=1;command(7,1,0,0);command(1,0x0c,0,0);
    effect[0]=2;command(0x0a,2,effect,sizeof(effect));tick(5);
    assert(s2_link.transport.count==2 && !motors[0] && !motors[1] && !s2_link.failed);
    tx_busy=0;tick(1);assert(s2_link.transport.count==1 && sent[1]==0x1a && sent[3]==7);
    /* Keep input temporarily unsubscribed to inspect the second reply. */
    uint8_t disable[]={0,0};write_value(0x0f,disable,2);tick(1);
    assert(!s2_link.transport.count && sent[1]==0x1a && sent[3]==1 && sent[6]==0x0c);
    write_value(0x0f,enable,2);
    /* Hold L+R and move both sticks, alternating with D-pad Up. Run across
     * many report-counter wraps and two wraps of the millisecond clock. */
    fixture_axes[0]=0;fixture_axes[1]=65535;fixture_axes[2]=65535;fixture_axes[3]=0;
    for(unsigned i=0;i<27000;++i) {
        uint8_t counter=s2_link.transport.counter;
        fixture_buttons=(i&1)?1u<<9:(1u<<10)|(1u<<11);
        tick(5);
        assert(s2_link.transport.connected && s2_link.transport.encrypted && !disconnects && !s2_link.failed);
        assert(led_state==1);
        assert(sent_size==66 && sent[1]==0xe && sent[3]==counter && s2_link.transport.counter==(uint8_t)(counter+1));
        assert(sent[5]==((i&1)?0:16) && sent[6]==((i&1)?8:16));
        assert(!memcmp(sent+8,"\xff\x0f\x00\x00\xf0\xff",6));
    }
    assert((s2_link.trace.flags&0xf800)==0xf800);
    for(unsigned i=0;i<4;++i) fixture_axes[i]=32767;
    fixture_buttons=1;
    memset(s2_test_registers,0,sizeof(s2_test_registers));programs=trace_programs=erases=disconnects=0;
}
static void stock_channel_layout(void) {
    /* Stock 0x12028 saves the four CONNECT_IND octets at +16 and a
     * byte-reversed radio-register copy at +20. Use the Core CSA #2 vector. */
    uint8_t state[24]={0};
    const uint8_t expected[]={25,20,6,21};
    memcpy(state+7,"\xff\xff\xff\xff\x1f",5);
    memcpy(state+16,"\xd6\xbe\x89\x8e\x8e\x89\xbe\xd6",8);
    s2_link.csa2=1;
    for(unsigned i=0;i<sizeof(expected);++i) {
        state[12]=(uint8_t)i;
        assert(s2_301b_channel(state)==expected[i] && state[5]==expected[i]);
    }
    s2_link.csa2=0;state[5]=17;
    assert(s2_301b_channel(state)==17);
}
int main(void) {
    uint8_t enable[2]={1,0},reply[3]={3,0,2},address[8]={0,1,1,2,3,4,5,6},key[17]={0};
    /* The stock dispatcher reads the live switch before its debounced reset.
     * On a 2.4G boot these BLE entry points can run before BLE initialization. */
    s2_301b_loop();
    assert(radio_polls==0);
    s2_301b_pair();
    assert(!s2_link.pairing && !R8(0x843428) && !s2_link.advertise_pending);
    fifo_result=1;s2_301b_init();assert(s2_link.failed);
    s2_301b_loop();s2_301b_pair();
    assert(!radio_polls && !disconnects && !s2_link.pairing);
    fifo_result=0;
    stock_channel_layout();
    {
        static const uint32_t chips[]={0x1160c8,0x1360c8,0x114485,0x136085};
        uint32_t saved;
        for(unsigned i=0;i<4;++i) {
            flash_id=chips[i];flash_sr=0xe2fc;flash_sr3=0x80;
            assert(s2_301b_flash_open(&saved));
            assert(flash_sr==(uint16_t)(0xe2fc&~protection_mask()) && flash_sr3==0x80);
            assert(s2_301b_flash_close(saved) && flash_sr==0xe2fc && flash_sr3==0x80);
            flash_sr=(uint16_t)(flash_sr&~protection_mask());unsigned writes=protection_writes;
            assert(s2_301b_flash_open(&saved) && s2_301b_flash_close(saved) && protection_writes==writes);
        }
        flash_id=0x136085;flash_sr3=4;unsigned writes=protection_writes;
        assert(!s2_301b_flash_open(&saved) && protection_writes==writes);
        flash_id=0x123456;assert(!s2_301b_flash_open(&saved) && protection_writes==writes);
        flash_id=0x1360c8;flash_sr=0x3c;flash_sr3=0;fail_unlock=1;
        assert(!s2_301b_flash_open(&saved) && flash_sr==0x3c);fail_unlock=0;
        assert(s2_301b_flash_open(&saved));fail_restore=1;
        assert(!s2_301b_flash_close(saved));fail_restore=0;
        assert(s2_301b_flash_close(saved) && flash_sr==0x3c);
    }
    console_startup();
    memset(flash,255,sizeof(flash));R32(0x842e50)=0xfe41;R32(0x842e3c)=0xfe5d;
    memcpy(B80_PTR(0x84386c),"ABCDEF",6);s2_301b_init();
    assert(receive_cb && ltk_cb && connect_cb && disconnect_cb && s2_link.pairing && !programs && !erases);
    assert(trace_programs==2 && !memcmp(flash,"S2DG\1\0",6) && flash[8]==1);
    tick(1);assert(trace_programs==4 && flash[64+8]==5);
    assert(flash_sr==0x3c);
    assert(!memcmp(ad,"\x02\x01\x06\x1b\xff\x53\x05",7));
    assert(s2_link.transport.mtu==23 && (R32(0x842e6c)&0x4100)==0x4100);
    connect_peer();tick(1);assert(sent_size==3 && sent[0]==2 && s2_link.mtu_offer==2);
    feed(reply,3);assert(s2_link.transport.mtu==512 && !s2_link.mtu_offer);
    write_value(0x1b,enable,2);write_value(0x0f,enable,2);
    command(0x15,1,address,8);tick(1);assert(sent[3]==0x15 && s2_unit.protocol.stage==1);
    for(unsigned i=1;i<17;++i) key[i]=(uint8_t)i;
    command(0x15,4,key,17);tick(1);assert(s2_unit.protocol.stage==2);
    command(0x15,2,key,17);tick(1);assert(s2_unit.protocol.stage==3);
    command(0x15,3,key,1);tick(1);assert(s2_unit.protocol.stage==4 && programs==2 && !s2_link.pairing && !erases);
    { uint8_t random[8]={0};assert(ltk_cb(0x40,random,0)==S2_OK); }
    R8(0x8435f2)=1;tick(1);
    R32(0x842e3c)=0x10f5d; /* Stock can select its radio-aware flash writer. */
    { /* The encrypted reconnect key exchange must durably replace both fields. */
        uint8_t refresh[22];for(unsigned i=0;i<22;++i) refresh[i]=(uint8_t)(0x70+i);
        command(3,7,refresh,22);tick(1);assert(s2_unit.protocol.stage==5 && programs==2);
        command(3,9,0,0);tick(1);assert(s2_unit.protocol.stage==4 && programs==4);
        for(unsigned i=0;i<6;++i) address[2+i]=refresh[5-i];
    }
    { /* LL enqueue has opposite success polarity to the ATT enqueue. */
        uint8_t req[5]={3,3,0x16,3,3},instant[7]={3,5,0x18,2,2,2,0};
        tx_busy=1;s2_301b_phy_control(req);s2_301b_phy_poll();assert(s2_link.phy_reply_pending);
        tx_busy=0;s2_301b_phy_poll();assert(!s2_link.phy_reply_pending);
        R8(0x843ac8)=0;R16(0x843ad4)=0xfffe;s2_301b_phy_instant(instant);
        assert(!s2_link.failed && R16(0x843ad6)==2 && R8(0x843b0e)==2 && R8(0x843ac8)==3);
        s2_301b_phy_instant(instant);assert(!s2_link.failed); /* repeated dispatch */
        instant[5]=3;R8(0x843ac8)=1;s2_301b_phy_instant(instant);assert(s2_link.failed);
        s2_link.failed=0;R8(0x843ac8)=0;instant[5]=0xfd;instant[6]=0xff;
        s2_301b_phy_instant(instant);assert(s2_link.failed);s2_link.failed=0;
    }
    { uint8_t features=0x23;command(0x0c,2,&features,1);tick(1); }
    { uint8_t effect[4]={3};command(0x0a,2,effect,4);tick(1);assert(motors[0] && motors[1]); }
    tick(6);assert(sent_size==66 && sent[0]==0x1b && sent[1]==0x0e && (sent[5]&0x40));
    R8(0x843a32)=0;disconnect_cb(4,0,0);assert(!motors[0] && !motors[1]);tick(1);
    assert(trace_programs==6 && flash[4*64+8]==2);
    assert(!s2_link.pairing && !memcmp(ad+17,address+2,6));
    s2_301b_init();assert(!s2_link.pairing && ad[16]==0x81 && programs==4);
    tick(1);
    assert(led_state==2 && flash[5*64+8]==1 && flash[6*64+8]==5);
    s2_301b_pair();assert(led_state==3);tick(1);
    assert(s2_link.pairing && ad[16]==0 && !memcmp(ad+17,"\0\0\0\0\0\0",6));
    assert(!disconnects && led_state==3 && flash[7*64+8]==3 && flash[7*64+38]==1);
    /* A central that opens a link but never initializes it must leave an
     * inspectable disconnect record, not a permanently solid status light. */
    R8(0x8435f2)=0;connect_peer();set_led(4,1);tick(1);assert(led_state==3);
    unsigned traces=trace_programs;
    tick(15000);assert(s2_link.failed && trace_programs==traces);
    tick(1);assert(disconnects==1 && !s2_link.transport.connected && s2_link.trace.pending);
    tick(1);assert(trace_programs==traces+2 && !s2_link.trace.pending);
    assert(flash[8*64+8]==2 && flash[8*64+9]==5 && flash[8*64+20]==0);
    connect_peer();tick(1);command(0xff,1,0,0);
    assert(!s2_link.failed && !s2_link.trace.error_source && !s2_link.transport.count);
    R8(0x843a32)=0;disconnect_cb(4,0,0);tick(1);
    assert(flash[9*64+9]==0 && flash[9*64+22]==0xff && flash[9*64+23]==1);
    /* Pair on a live link must await termination and preserve both records. */
    connect_peer();s2_301b_pair();traces=trace_programs;
    tx_busy=1;tick(1);assert(s2_link.transport.connected && trace_programs==traces);
    tx_busy=0;tick(1);assert(!s2_link.transport.connected && trace_programs==traces);
    tick(1);assert(flash[10*64+8]==2 && flash[11*64+8]==3 && led_state==3);
    for(unsigned i=0;i<20;++i) {
        connect_peer();R8(0x843a32)=0;disconnect_cb(4,0,0);tick(1);
    }
    traces=trace_programs;
    connect_peer();R8(0x843a32)=0;disconnect_cb(4,0,0);tick(1);
    assert(s2_link.trace.stored==16 && trace_programs==traces && !erases && programs==4);
    assert(flash_sr==0x3c);
    puts("PASS: target startup, unchanged FIFO geometry, MTU exchange, fragmented pairing, durable bond, LTK binding, controls, rumble stop and reboot reconnect against simulated stock ABI");return 0;
}

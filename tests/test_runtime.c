/* SPDX-License-Identifier: MIT */
#include "s2_transport.h"
#include "s2_rumble.h"
#include "s2_input_301b.h"
#include "s2_csa2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int busy;uint8_t sent[109];size_t length;uint16_t handle; } sink;
static int notify(void *ctx,uint16_t h,const uint8_t *p,size_t n) {
    sink *s=ctx;if(s->busy) return S2_BUSY;
    memcpy(s->sent,p,n);s->length=n;s->handle=h;return S2_OK;
}
static int read_flash(void *ctx,uint32_t a,uint8_t *p,size_t n) {
    size_t i;(void)ctx;for(i=0;i<n;++i) p[i]=(uint8_t)(a+i);return 1;
}
static void queue_test(void) {
    s2_session session;s2_transport t;sink s={0};uint8_t mac[6]={0},cccd[2]={1,0};
    uint8_t p[49]={0};s2_input input={{0},2048,2048,2048,2048,50,0,0};
    s2_init(&session,mac,0,read_flash,0);s2_transport_init(&t,&session,notify,&s);
    s2_transport_connect(&t);
    assert(s2_transport_subscribe(&t,0x1f,cccd,2)==0);
    assert(s2_transport_subscribe(&t,0xf,cccd,2)==0);
    p[33]=2;p[34]=0x91;p[35]=4;p[36]=4;p[38]=8;p[41]=79;p[46]=0x30;p[47]=1;
    assert(s2_transport_command(&t,0x16,p,49)==0);
    p[33]=0x11;p[36]=3;p[38]=0;
    assert(s2_transport_command(&t,0x16,p,41)==0 && t.count==2);
    assert(s2_transport_command(&t,0x16,p,41)==S2_BUSY);
    assert(s2_transport_poll(&t,&input)==S2_MTU && t.count==2);
    t.mtu=512;s.busy=1;
    assert(s2_transport_poll(&t,&input)==S2_BUSY && t.count==2);
    s.busy=0;assert(s2_transport_poll(&t,&input)==0 && t.count==1);
    assert(s.length==109 && s.sent[14]==2 && s.sent[30]==0 && s.sent[108]==78);
    assert(s2_transport_poll(&t,&input)==0 && t.count==0 && s.length==51);
    assert(s.sent[14]==0x11 && s.sent[22]==1 && s.sent[50]==0x41);
    assert(s2_transport_poll(&t,&input)==S2_LINK); /* cannot stream before encryption */
    t.encrypted=1;s.busy=1;assert(s2_transport_poll(&t,&input)==S2_BUSY && t.counter==0);
    s.busy=0;assert(s2_transport_poll(&t,&input)==0 && t.counter==1 && s.handle==0xe);
    p[33]=0x16;p[36]=1;
    assert(s2_transport_command(&t,0x16,p,41)==0);
    assert(s2_transport_poll(&t,0)==0 && s.length==46);
    for(unsigned i=22;i<46;++i) assert(s.sent[i]==0);
    s2_transport_disconnect(&t);assert(!t.count && !t.encrypted && !t.subscriptions);
}
static void inputs_test(void) {
    const uint8_t expected[18]={6,15,7,14,2,3,9,10,8,11,12,4,1,0,13,5,255,16};
    s2_calibrated_301b raw={0};s2_input input={0};
    input.battery_percent=77;
    for(unsigned i=0;i<18;++i) {
        uint32_t bits;raw.buttons=1u<<i;s2_convert_301b(&input,&raw);
        bits=(uint32_t)input.buttons[0]|((uint32_t)input.buttons[1]<<8)|((uint32_t)input.buttons[2]<<16);
        assert(bits==(expected[i]==255?0u:1u<<expected[i]));
    }
    raw.axes[0]=0;raw.axes[1]=65535;raw.axes[2]=32767;raw.axes[3]=32768;
    s2_convert_301b(&input,&raw);
    assert(input.lx==4095 && input.ly==0 && input.rx==2048 && input.ry==2047);
    assert(input.battery_percent==77);
}
static void rumble_test(void) {
    s2_rumble_player player;uint8_t packet[33]={0},out[2];
    s2_rumble_stop(&player);
    packet[1]=0x71; /* 3 samples, sequence 1 */
    packet[3]=0xfc;packet[4]=0x0f; /* high amplitude 1023 */
    packet[8]=0x00;packet[9]=0x08; /* high amplitude 512 */
    assert(s2_rumble_submit(&player,packet,33,65530)==0);
    s2_rumble_output(&player,65530,out);assert(out[0]==100 && out[1]==0);
    s2_rumble_output(&player,65535,out);assert(out[0]==50);
    s2_rumble_output(&player,4,out);assert(out[0]==0);
    assert(s2_rumble_submit(&player,packet,33,5)==0); /* retransmit must not restart */
    s2_rumble_output(&player,5,out);assert(out[0]==0);
    packet[1]=0x72;assert(s2_rumble_submit(&player,packet,33,6)==0);
    s2_rumble_output(&player,6,out);assert(out[0]==100);
    s2_rumble_output(&player,56,out);assert(out[0]==0 && !player.valid[0]);
    assert(s2_rumble_preset(&player,3,100)==0);
    s2_rumble_output(&player,100,out);assert(out[0] && out[1]);
    s2_rumble_stop(&player);s2_rumble_output(&player,101,out);assert(!out[0] && !out[1]);
    packet[17]=0x80;assert(s2_rumble_submit(&player,packet,33,103)==S2_INVALID);
    s2_rumble_output(&player,103,out);assert(!out[0] && !out[1]);
}
static void channels_test(void) {
    const uint8_t all[5]={255,255,255,255,31},sparse[5]={0,6,0xe0,0,0x1e};
    const uint8_t first[4]={25,20,6,21},second[4]={35,9,33,21};
    uint8_t invalid[5]={0};
    for(unsigned i=0;i<4;++i) {
        assert(s2_csa2_channel(all,(uint16_t)i,0x305f)==first[i]);
        assert(s2_csa2_channel(sparse,(uint16_t)i,0x305f)==second[i]);
    }
    assert(s2_csa2_channel(invalid,0,0)==255);
    for(unsigned i=0;i<65536;++i) {
        unsigned channel=s2_csa2_channel(sparse,(uint16_t)i,0x305f);
        assert(channel<37 && (sparse[channel>>3]&(1u<<(channel&7))));
    }
}
int main(void) {
    queue_test();inputs_test();rumble_test();channels_test();
    puts("PASS: queued initialization under backpressure, encrypted input gate, stock input mapping, rumble scheduling and CSA2 sample vectors");return 0;
}

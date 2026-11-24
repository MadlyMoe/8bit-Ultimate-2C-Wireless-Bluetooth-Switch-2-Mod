/* SPDX-License-Identifier: MIT
 * Exact Wireless model 301B / application v1.09 adapter. No other image may
 * link these addresses. See docs/STOCK_ABI.md and the checked patch manifest.
 */
#include "runtime_301b.h"
#include "s2_stock.h"
#include "s2_virtual_flash.h"
#include <string.h>

s2_301b_session s2_unit __attribute__((section(".overlay.gatt")));
s2_301b_link s2_link __attribute__((section(".overlay.smp")));
s2_301b_actuators s2_actuators __attribute__((section(".overlay.l2cap")));
s2_att_state s2_att __attribute__((section(".overlay.att")));
#ifndef S2_HOST_TARGET_TEST
typedef char session_fits[sizeof(s2_301b_session)<=60?1:-1];
typedef char link_fits[sizeof(s2_301b_link)<=212?1:-1];
typedef char actuators_fit[sizeof(s2_301b_actuators)<=32?1:-1];
typedef char att_fits[sizeof(s2_att_state)==32?1:-1];
#endif

#define BOND_BASE 0x3d000u
#define BOND_SIZE 0x2000u
static uint16_t milliseconds(void) { return s2_actuators.elapsed_ms; }
static void clock_update(void) {
    uint32_t tick=R32(0x800740u),delta=tick-s2_actuators.last_tick;
    if(delta>=16000u) {
        uint32_t ms=delta/16000u;
        s2_actuators.last_tick+=ms*16000u;
        s2_actuators.elapsed_ms=(uint16_t)(s2_actuators.elapsed_ms+ms);
    }
}
static void led(uint8_t state) { FN(0xd3d8u,void,(uint8_t,uint8_t))(4,state); }
enum { LED_CONNECTED=1, LED_RECONNECT=2, LED_PAIRING=3 };
enum { ADV_NONE, ADV_RESTART, ADV_PAIR_BUTTON, ADV_WAKE_EXPIRED, ADV_WAIT_DISCONNECT, ADV_FIRST_POLL };
enum { TRACE_BOOT=1, TRACE_DISCONNECTED, TRACE_PAIR_BUTTON, TRACE_WAKE_EXPIRED, TRACE_FIRST_POLL };
static void indicate_setup(void) {
    uint8_t state=s2_link.pairing?LED_PAIRING:LED_RECONNECT;
    if(R8(0x843457u)!=state) led(state);
}
static void indicate_link(void) {
    if(s2_link.transport.encrypted && (s2_link.transport.subscriptions&1)) {
        if(R8(0x843457u)!=LED_CONNECTED) led(LED_CONNECTED);
    } else indicate_setup();
}
static void motor_stop(void) {
    s2_rumble_stop(&s2_actuators.player);
    s2_actuators.motor[0]=s2_actuators.motor[1]=0;
    s2_stock_motor_stop();
}
static int flash_read(void *ctx,uint32_t offset,uint8_t *out,size_t n) {
    void (*read)(uint32_t,uint32_t,uint8_t *);
    (void)ctx;
    if(offset>BOND_SIZE || n>BOND_SIZE-offset) return 0;
    if(!R32(0x842e50u)) return 0;
    read=FN(R32(0x842e50u),void,(uint32_t,uint32_t,uint8_t *));
    read(BOND_BASE+offset,(uint32_t)n,out);return 1;
}
static int flash_program(void *ctx,uint32_t offset,const uint8_t *p,size_t n) {
    void (*program)(uint32_t,uint32_t,const uint8_t *);
    uint32_t saved,address=R32(0x842e3cu);
    (void)ctx;
    if(offset>=BOND_SIZE || !n || n>BOND_SIZE-offset || n>64 ||
       (offset&255u)+n>256u) return 0;
    if(address!=0xfe5du && address!=0x10f5du) return 0;
    if(!s2_301b_flash_open(&saved)) return 0;
    program=FN(address,void,(uint32_t,uint32_t,const uint8_t *));
    program(BOND_BASE+offset,(uint32_t)n,p);
    return s2_301b_flash_close(saved);
}
static int flash_erase(void *ctx,unsigned sector) {
    uint32_t saved;
    (void)ctx;
    /* Never erase while advertising or connected; preparation owns this gate. */
    if(sector>1 || R8(0x843a32u)!=0) return 0;
    if(!s2_301b_flash_open(&saved)) return 0;
    FN(0xfe98u,void,(uint32_t))(BOND_BASE+sector*0x1000u);
    return s2_301b_flash_close(saved);
}
static const s2_bond_io bond_io={flash_read,flash_program,flash_erase,0,0x1000};
static void trace_word(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void trace_capture(uint8_t event,const uint8_t *data,int n) {
    /* RX scratch is reused only after the link has stopped, or before its
     * first advertisement. No packet data, addresses or keys are persisted. */
    uint8_t *p=s2_link.rx.bytes;
    memset(p,0,32);p[0]=event;p[1]=s2_link.trace.error_source;
    p[2]=(uint8_t)s2_link.trace.result;p[3]=s2_unit.protocol.stage;
    trace_word(p+4,s2_link.trace.flags);trace_word(p+6,milliseconds());
    trace_word(p+8,s2_link.trace.rx_count);trace_word(p+10,s2_link.trace.tx_count);
    p[12]=s2_link.trace.opcode;p[13]=s2_link.trace.handle;
    p[14]=s2_link.trace.command;p[15]=s2_link.trace.subcommand;
    trace_word(p+16,s2_att.mtu);p[18]=s2_link.transport.subscriptions;
    p[19]=s2_link.transport.encrypted;p[20]=s2_link.csa2;p[21]=R8(0x843b0du);
    trace_word(p+22,R16(0x843ad4u));p[24]=s2_link.transport.count;p[25]=s2_att.pending;
    p[26]=s2_link.pairing;p[27]=s2_link.failed;p[28]=s2_link.mtu_offer;
    p[29]=R8(0x843a32u);p[30]=(uint8_t)(n>0 && data?data[0]:0);
    p[31]=(uint8_t)(n>1 && data?data[1]:0);
    if(event==TRACE_DISCONNECTED && (s2_link.trace.flags&S2_TRACE_LED_LENGTHS))
        p[31]=s2_link.led_lengths;
    s2_link.trace.pending=1;
}
static void trace_flush(void) {
    /* A bounded journal append only while the radio is off. Leave two blank
     * slots for bond creation/refresh, never erase for diagnostics, and cap wear per boot. */
    if(!s2_link.trace.pending || s2_link.transport.connected || R8(0x843a32u)) return;
    if(s2_link.trace.stored<16 && s2_bond_trace_append(&bond_io,s2_link.rx.bytes)==S2_OK)
        ++s2_link.trace.stored;
    s2_link.trace.pending=0;
}
static int load_bond(s2_bond_store *store,s2_saved_bond *bond) {
    return s2_bond_open(store,&bond_io,bond);
}
static int commit_bond(void *ctx,const s2_bond *bond) {
    s2_bond_store store;s2_saved_bond saved;int rc;
    (void)ctx;
    if(!s2_link.transport.connected ||
       (!s2_link.pairing && !s2_link.transport.encrypted)) return 0;
    rc=load_bond(&store,&saved);if(rc!=S2_OK && rc!=S2_STORE_EMPTY) return 0;
    saved.protocol=*bond;memcpy(saved.peer_le,s2_unit.peer,6);
    saved.peer_type=s2_unit.peer_type;
    if(s2_bond_save(&store,&saved)!=S2_OK) return 0;
    s2_link.trace.flags|=S2_TRACE_BOND;
    s2_link.pairing=0;R8(0x843428u)=0;return 1;
}
static int read_virtual(void *ctx,uint32_t a,uint8_t *out,size_t n) {
    (void)ctx;return s2_virtual_flash_read(s2_unit.protocol.local_le,a,out,n);
}
static int ltk(uint16_t connection,const uint8_t *random,uint16_t ediv) {
    s2_bond_store store;s2_saved_bond bond;int rc;
    s2_link.trace.flags|=S2_TRACE_LTK_REQUEST;
    if(load_bond(&store,&bond)!=S2_OK) {
        s2_link.trace.error_source=4;s2_link.trace.result=S2_SEQUENCE;
        FN(0x13114u,int,(uint16_t))(connection);return S2_SEQUENCE;
    }
    rc=s2_stock_ltk_reply(connection,&bond,s2_unit.peer_type,s2_unit.peer,random,ediv);
    if(rc==S2_OK) s2_link.trace.flags|=S2_TRACE_LTK_OK;
    else { s2_link.trace.error_source=4;s2_link.trace.result=(int8_t)rc; }
    return rc;
}
static int enqueue(const uint8_t *head,unsigned hn,const uint8_t *data,unsigned n) {
    int rc;
    if(!s2_link.transport.connected) return S2_LINK;
    R16(0x842f22u)=s2_att.mtu;
    rc=FN(0x1484cu,int,(uint16_t,uint16_t,const uint8_t *,int,const uint8_t *,int))
        ((uint16_t)(R16(0x843b26u)|0x8000u),4,head,(int)hn,data,(int)n);
    if(!rc && s2_link.trace.tx_count!=65535) ++s2_link.trace.tx_count;
    return rc==0?S2_OK:rc==0x80?S2_LINK:S2_BUSY;
}
static int att_send(void *ctx,const uint8_t *p,size_t n) {
    (void)ctx;if(!n) return S2_INVALID;
    return enqueue(p,1,p+1,(unsigned)n-1);
}
static int notify(void *ctx,uint16_t handle,const uint8_t *p,size_t n) {
    uint8_t header[3]={0x1b,(uint8_t)handle,(uint8_t)(handle>>8)};
    (void)ctx;return enqueue(header,3,p,(unsigned)n);
}
static void sample_input(s2_input *in) {
    s2_calibrated_301b raw;
    memset(in,0,sizeof(*in));s2_stock_read_calibrated(&raw);s2_convert_301b(in,&raw);
    in->battery_percent=s2_stock_battery_percent();
    /* Power/charging bits remain unbound until their stock writers are traced. */
}
static int descriptor_index(uint16_t h) {
    static const uint8_t handles[]={0x0c,0x10,0x1c,0x20,0x24,0x28,0x30};
    unsigned i;for(i=0;i<sizeof(handles);++i) if(handles[i]==h) return (int)i;
    return -1;
}
static int cccd_index(uint16_t h) {
    static const uint8_t handles[]={0x0b,0x0f,0x1b,0x1f,0x23,0x27,0x2f,0x3b};
    unsigned i;for(i=0;i<sizeof(handles);++i) if(handles[i]==h) return (int)i;
    return -1;
}
static int att_read(void *ctx,uint16_t handle,uint16_t offset,uint8_t *out,size_t cap,size_t *len) {
    int i;uint8_t pair[2];const uint8_t *p=pair;size_t size=2;
    (void)ctx;
    if(handle==S2_HANDLE_INPUT) {
        uint8_t report[S2_REPORT_SIZE];s2_input input;
        if(!s2_link.transport.encrypted) return 5;
        if(offset>S2_REPORT_SIZE) return 7;
        sample_input(&input);s2_report(report,&input,s2_link.transport.counter);
        *len=S2_REPORT_SIZE;if(cap>S2_REPORT_SIZE-offset) cap=S2_REPORT_SIZE-offset;
        memcpy(out,report+offset,cap);return 0;
    }
    i=descriptor_index(handle);
    if(i>=0) p=s2_link.descriptors+2*i;
    else {
        i=cccd_index(handle);if(i<0) return 2;
        pair[0]=s2_link.cccd[i];pair[1]=0;
    }
    if(offset>size) return 7;
    *len=size;if(cap>size-offset) cap=size-offset;memcpy(out,p+offset,cap);return 0;
}
static int att_write(void *ctx,uint16_t h,const uint8_t *p,size_t n) {
    int i,rc;size_t offset=h==S2_HANDLE_COMBINED?33u:0u;
    (void)ctx;
    /* First write in the captured Pro2 console startup sequence. It enables
     * the vendor session; this adapter is already in that session. Reject
     * malformed values, but acknowledge the supported 01 00 setup value. */
    if(h==5) {
        if(n!=2 || p[0]!=1 || p[1]!=0) return 13;
        s2_link.trace.flags|=S2_TRACE_SETUP;return 0;
    }
    i=cccd_index(h);
    if(i>=0) {
        if(n!=2 || p[1] || p[0]>(h==0x3b?2:1) || (h==0x3b && p[0]==1)) return 13;
        if(h==0x0f || h==0x1b || h==0x1f)
            if(s2_transport_subscribe(&s2_link.transport,h,p,n)!=S2_OK) return 14;
        s2_link.cccd[i]=p[0];return 0;
    }
    i=descriptor_index(h);
    if(i>=0) { if(n!=2) return 13;memcpy(s2_link.descriptors+2*i,p,2);return 0; }
    if(h==0x12) {
        if(!s2_link.transport.encrypted) return 5;
        if(!(s2_unit.protocol.features&S2_FEATURE_RUMBLE)) return 0;
        return s2_rumble_submit(&s2_actuators.player,p,n,milliseconds())==S2_OK?0:13;
    }
    if(h!=S2_HANDLE_COMMAND && h!=S2_HANDLE_COMBINED) return 3;
    if(n<offset+8) return 13;
    s2_link.trace.command=p[offset];s2_link.trace.subcommand=p[offset+3];
    if(p[offset]==9 && (p[offset+3]==7 || p[offset+3]==8)) {
        size_t actual=n-offset-8;
        unsigned declared=p[offset+5];
        s2_link.led_lengths=(uint8_t)(((declared>15?15:declared)<<4)|(actual>15?15:actual));
        s2_link.trace.flags|=S2_TRACE_LED_LENGTHS;
    }
    if(p[offset]==0x15 && !s2_link.pairing) return 5;
    if(p[offset]==3 && (p[offset+3]==7 || p[offset+3]==9) &&
       !s2_link.pairing && !s2_link.transport.encrypted) return 5;
    rc=s2_transport_command(&s2_link.transport,h,p,n);
    if(rc!=S2_OK) {
        s2_link.trace.flags|=rc==S2_BUSY?S2_TRACE_REPLY_BUSY:S2_TRACE_REJECTED_WRITE;
        if(rc==S2_BUSY) return 17;
        if(rc==S2_SHORT || rc==S2_INVALID) return 13;
        return rc==S2_UNSUPPORTED?6:14;
    }
    /* Disabling gameplay vibration must silence a sample already in flight. */
    if(p[offset]==0x0c && !(s2_unit.protocol.features&S2_FEATURE_RUMBLE) &&
       (p[offset+3]==2 || p[offset+3]==3 || p[offset+3]==5) &&
       n>offset+8 && (p[offset+8]&S2_FEATURE_RUMBLE || p[offset+3]==2))
        s2_rumble_stop(&s2_actuators.player);
    if(offset && s2_link.transport.encrypted &&
       (s2_unit.protocol.features&S2_FEATURE_RUMBLE))
        s2_rumble_submit(&s2_actuators.player,p,33,milliseconds());
    return 0;
}
static const s2_att_io att_io={att_read,att_write,att_send};
static int receive(uint16_t connection,const uint8_t *ll) {
    const uint8_t *att;size_t size;int rc;
    (void)connection;
    /* The unchanged 64-byte DMA slot bounds the LL payload. CID 6 (SMP) is
     * deliberately rejected: console pairing provisions the LTK via GATT. */
    if(s2_link.trace.rx_count!=65535) ++s2_link.trace.rx_count;
    if(ll[1]>27) { s2_link.trace.error_source=1;s2_link.failed=1;return 0; }
    if((ll[0]&3)==2 && ll[1]>=4 && (ll[4]!=4 || ll[5])) return 0;
    rc=s2_l2cap_feed(&s2_link.rx,ll,(size_t)ll[1]+2,&att,&size);
    if(rc==1) {
        s2_link.trace.flags|=S2_TRACE_ATT;s2_link.trace.opcode=att[0];
        s2_link.trace.last_ms=milliseconds();
        if(size>=3 && (att[0]==0x12 || att[0]==0x52 || att[0]==0x0a || att[0]==0x0c))
            s2_link.trace.handle=att[1];
        s2_link.transport.encrypted=R8(0x8435f2u)!=0;
        if(size==3 && att[0]==3 && s2_link.mtu_offer==2) {
            unsigned mtu=(unsigned)att[1]|((unsigned)att[2]<<8);
            if(mtu<23) { s2_link.trace.error_source=2;s2_link.failed=1;return 0; }
            s2_att.mtu=(uint16_t)(mtu>512?512:mtu);
            s2_link.transport.mtu=s2_att.mtu;R16(0x842f22u)=s2_att.mtu;
            s2_link.trace.flags|=S2_TRACE_MTU;s2_link.mtu_offer=0;return 0;
        }
        rc=s2_att_receive(&s2_att,&att_io,0,att,size);
        s2_link.transport.mtu=s2_att.mtu;R16(0x842f22u)=s2_att.mtu;
        if(s2_att.mtu>23) s2_link.trace.flags|=S2_TRACE_MTU;
        if(rc!=S2_OK) {
            if(!s2_link.trace.error_source) { s2_link.trace.error_source=2;s2_link.trace.result=(int8_t)rc; }
            s2_link.failed=1;
        }
    } else if(rc<0) { s2_link.trace.error_source=1;s2_link.trace.result=(int8_t)rc;s2_link.failed=1; }
    return 0;
}
static int set_leds(void *ctx,uint8_t mask,uint8_t flashing) {
    /* Keep the console's player selection in the protocol session. The
     * single physical indicator shows whether the controller is ready. */
    (void)ctx;(void)mask;(void)flashing;indicate_link();
    return 1;
}
static int battery(void *ctx,uint16_t *mv,uint8_t status[4]) {
    (void)ctx;*mv=s2_stock_battery_mv();memset(status,0,4);return 1;
}
static int rumble_sample(void *ctx,uint8_t effect) {
    (void)ctx;
    if(!s2_link.transport.encrypted) return 0;
    return s2_rumble_preset(&s2_actuators.player,effect,milliseconds())==S2_OK;
}
static int rumble_command(void *ctx,const uint8_t data[20]) {
    static const uint8_t startup[20]={1,255,255,255,255,255,255,255,255,0x35,0,0x46,0,0,0,0,0,0,0,0};
    (void)ctx;(void)data;
    /* Captured Pro2 startup packet. The reference adapter acknowledges this
     * without motor output. Accept only that exact compatibility probe; the
     * opaque 20-byte format is not an implemented ERM waveform interface.
     * Actual vibration is driven by documented presets and 33-byte streams. */
    return !memcmp(data,startup,sizeof(startup));
}
static int initialize(void *ctx,uint8_t sub,const uint8_t *p,size_t n) {
    (void)ctx;
    /* 0x03/0a selects report 09 (Pro2); native Bluetooth always uses its
     * corresponding input characteristic. Other USB-only operations reject. */
    return sub==0x0a && n==4 && p[0]==9;
}
static const uint8_t firmware_info[12]={2,1,4,2,12,0,0,0,255,255,255,0};
static const s2_hardware hardware={set_leds,battery,rumble_sample,rumble_command,initialize,firmware_info};
static void enable_advertising(void) {
    s2_link.advertise_pending=ADV_NONE;
    FN(0x11598u,int,(int))(1);
}
static void advertise(void) {
    s2_bond_store store;s2_saved_bond bond;uint8_t ad[S2_ADV_SIZE];int rc;
    uint8_t prior_state[2]={R8(0x843a32u),R8(0x843457u)};
    static const uint8_t name[]={19,9,'8','B','i','t','D','o',' ','U','l','t','i','m','a','t','e',' ','2','C'};
    FN(0x11598u,int,(int))(0);
    s2_301b_phy_switch(1);
    rc=load_bond(&store,&bond);
    if(rc!=S2_OK && rc!=S2_STORE_EMPTY) { s2_link.failed=1;return; }
    if(s2_bond_prepare(&store)!=S2_OK) { s2_link.failed=1;return; }
    trace_flush();
    if(rc==S2_STORE_EMPTY) s2_link.pairing=1;
    if((s2_link.advertise_pending==ADV_PAIR_BUTTON || s2_link.advertise_pending==ADV_WAKE_EXPIRED)
        && !R8(0x843a32u)) {
        uint8_t event=s2_link.advertise_pending==ADV_PAIR_BUTTON?TRACE_PAIR_BUTTON:TRACE_WAKE_EXPIRED;
        trace_capture(event,prior_state,sizeof(prior_state));trace_flush();
    }
    s2_advertisement(ad,!s2_link.pairing && rc==S2_OK?&bond.protocol:0,
        s2_link.wake && !s2_link.pairing && rc==S2_OK);
    FN(0x116c0u,int,(unsigned,unsigned,unsigned,unsigned,unsigned,const uint8_t *,unsigned,unsigned))
        (0x30,0x38,0,0,0,0,7,0);
    FN(0x11614u,int,(const uint8_t *,unsigned))(ad,sizeof(ad));
    FN(0x11690u,int,(const uint8_t *,unsigned))(name,sizeof(name));
    R8(0x842e7cu)|=0x20; /* ADV_IND ChSel: CSA2 support. */
    indicate_setup();
    if(s2_link.advertise_pending!=ADV_FIRST_POLL) enable_advertising();
}
static void connected(unsigned event,const uint8_t *data,int n) {
    uint8_t stored=s2_link.trace.stored;
    (void)event;(void)data;(void)n;
    s2_transport_connect(&s2_link.transport);s2_att_reset(&s2_att);
    memset(s2_link.cccd,0,sizeof(s2_link.cccd));memset(s2_link.descriptors,0,sizeof(s2_link.descriptors));
    memset(&s2_link.rx,0,sizeof(s2_link.rx));R32(0x8436bcu)=1;R16(0x842f22u)=23;
    memset(&s2_link.trace,0,sizeof(s2_link.trace));s2_link.trace.stored=stored;
    s2_link.trace.flags=S2_TRACE_CONNECTED;s2_link.trace.last_ms=milliseconds();
    s2_link.failed=0;s2_link.last_input=R32(0x800740u);indicate_setup();
    if(s2_link.advertise_pending!=ADV_PAIR_BUTTON) s2_link.advertise_pending=ADV_NONE;
    s2_link.mtu_offer=1;s2_link.wake=0;
}
static void disconnected(unsigned event,const uint8_t *data,int n) {
    (void)event;
    trace_capture(TRACE_DISCONNECTED,data,n);
    motor_stop();s2_transport_disconnect(&s2_link.transport);s2_att_reset(&s2_att);
    s2_link.rx.used=s2_link.rx.expected=0;s2_link.csa2=0;R32(0x8436bcu)=0;
    s2_link.advertise_pending=(s2_link.advertise_pending==ADV_PAIR_BUTTON ||
        s2_link.advertise_pending==ADV_WAIT_DISCONNECT)?ADV_PAIR_BUTTON:ADV_RESTART;
    indicate_setup();
    s2_link.mtu_offer=0;s2_link.wake=0;s2_link.failed=0;s2_link.phy_reply_pending=0;
}
void s2_301b_pair(void) {
    if(s2_unit.protocol.hardware!=&hardware) return;
    s2_link.pairing=1;s2_link.wake=0;s2_link.pair_started=milliseconds();R8(0x843428u)=1;
    s2_link.advertise_pending=ADV_PAIR_BUTTON;indicate_setup();motor_stop();
}
void s2_301b_init(void) {
    memset(&s2_unit,0,sizeof(s2_unit));memset(&s2_link,0,sizeof(s2_link));
    memset(&s2_actuators,0,sizeof(s2_actuators));s2_att_reset(&s2_att);
    s2_actuators.last_tick=R32(0x800740u);
    s2_init(&s2_unit.protocol,(const uint8_t *)B80_PTR(0x84386cu),commit_bond,read_virtual,0);
    s2_transport_init(&s2_link.transport,&s2_unit.protocol,notify,0);
    FN(0x111e8u,void,(void))();
    FN(0x11188u,void,(const uint8_t *))(s2_unit.protocol.local_le);
    FN(0x114f8u,void,(void))();FN(0x121a0u,void,(void))();
    FN(0x11f40u,void,(unsigned,unsigned))(27,27);
    FN(0x11e64u,void,(void *,unsigned,unsigned))((void *)0x843178u,40,8);
    FN(0x11ea4u,void,(void *,unsigned,unsigned))((void *)0x8432b8u,64,4);
    if(FN(0x11450u,int,(void))()!=0) { s2_link.failed=1;return; }
    /* Never call stock GATT/SMP init: their objects are the overlays above. */
    R32(0x843530u)=R32(0x843534u)=0;
    R32(0x843660u)=R32(0x843668u)=R32(0x843684u)=0;
    FN(0x14780u,void,(void *))((void *)receive);
    FN(0x121bcu,void,(void *))((void *)ltk);
    FN(0x110d4u,void,(unsigned,void *))(3,(void *)connected);
    FN(0x110d4u,void,(unsigned,void *))(4,(void *)disconnected);
    FN(0x110d4u,void,(unsigned,void *))(15,(void *)0xa065u);
    FN(0x13478u,void,(unsigned))(0);FN(0x10994u,void,(uint8_t))(0xb9);
    s2_link.wake=1;s2_link.pairing=R8(0x843428u)!=0;
    s2_301b_phy_init();motor_stop();
    s2_bind_hardware(&s2_unit.protocol,&hardware);
    s2_link.advertise_pending=ADV_FIRST_POLL;
    trace_capture(TRACE_BOOT,0,0);advertise();
}
void s2_301b_loop(void) {
    uint8_t motors[2];uint32_t tick;
    /* Stock polls the live radio selector before its debounced mode reset.
     * Leave radio and overlay state alone until BLE setup has bound this unit. */
    if(s2_unit.protocol.hardware!=&hardware) return;
    clock_update();FN(0x11220u,void,(void))();
    if(s2_link.failed) { motor_stop();FN(0x12438u,int,(uint8_t))(0x13);return; }
    if(s2_link.transport.connected && s2_link.advertise_pending==ADV_PAIR_BUTTON) {
        s2_link.advertise_pending=ADV_WAIT_DISCONNECT;
        if(FN(0x12438u,int,(uint8_t))(0x13)!=0 && s2_link.transport.connected)
            s2_link.advertise_pending=ADV_PAIR_BUTTON;
        return;
    }
    if(s2_link.advertise_pending==ADV_WAIT_DISCONNECT) return;
    if(!s2_link.transport.connected) {
        if(s2_link.advertise_pending==ADV_FIRST_POLL) {
            /* Initial configuration is complete. Record the first return from
             * the stock foreground poll before the radio can accept a link. */
            uint8_t state[2]={R8(0x843a32u),R8(0x843457u)};
            trace_capture(TRACE_FIRST_POLL,state,sizeof(state));trace_flush();
            enable_advertising();
        } else if(s2_link.advertise_pending) advertise();
        indicate_setup();
        if(s2_link.wake && milliseconds()>3000u) { s2_link.wake=0;s2_link.advertise_pending=ADV_WAKE_EXPIRED; }
        if(s2_link.pairing && (uint16_t)(milliseconds()-s2_link.pair_started)>=60000u) {
            s2_link.pairing=0;R8(0x843428u)=0;motor_stop();
            FN(0xd8e8u,void,(void))();
        }
        return;
    }
    s2_link.transport.encrypted=R8(0x8435f2u)!=0;
    if(R8(0x843b0du)==2) s2_link.trace.flags|=S2_TRACE_PHY_2M;
    /* A stalled setup must eventually release the link so its diagnostics can
     * be saved with the radio stopped. Working input connections never time out. */
    if(!(s2_link.transport.encrypted && (s2_link.transport.subscriptions&1)) &&
       (uint16_t)(milliseconds()-s2_link.trace.last_ms)>=15000u) {
        s2_link.trace.error_source=5;s2_link.trace.result=0;s2_link.failed=1;return;
    }
    s2_301b_phy_poll();
    s2_att_poll(&s2_att,&att_io,0);
    if(s2_link.mtu_offer==1 && !s2_att.pending) {
        const uint8_t request[3]={2,0,2};
        if(att_send(0,request,sizeof(request))==S2_OK) s2_link.mtu_offer=2;
    }
    if(!s2_att.pending) s2_transport_poll(&s2_link.transport,0);
    tick=R32(0x800740u);
    if(tick-s2_link.last_input>=80000u && !s2_att.pending && !s2_link.transport.count) {
        s2_input input;
        FN(0xb388u,void,(void))();FN(0xb14cu,void,(void))();sample_input(&input);
        if(s2_transport_poll(&s2_link.transport,&input)==S2_OK) {
            s2_link.last_input=tick;s2_link.trace.flags|=S2_TRACE_INPUT;
            if(input.buttons[0] || input.buttons[1] || input.buttons[2])
                s2_link.trace.flags|=S2_TRACE_BUTTON;
            if(input.buttons[1]&15) s2_link.trace.flags|=S2_TRACE_DPAD;
        }
    }
    s2_rumble_output(&s2_actuators.player,milliseconds(),motors);
    /* Gameplay streams are gated at receipt. The explicit, encrypted Search
     * preset works independently of the gameplay feature mask. */
    if(!s2_link.transport.encrypted) motors[0]=motors[1]=0;
    if(memcmp(motors,s2_actuators.motor,2)) {
        if(motors[0] || motors[1]) s2_link.trace.flags|=S2_TRACE_MOTOR_OUTPUT;
        memcpy(s2_actuators.motor,motors,2);s2_stock_motor(motors[0],motors[1]);
    }
    indicate_link();
}

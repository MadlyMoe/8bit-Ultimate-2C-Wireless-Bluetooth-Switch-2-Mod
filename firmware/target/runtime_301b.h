/* SPDX-License-Identifier: MIT */
#ifndef S2_RUNTIME_301B_H
#define S2_RUNTIME_301B_H
#include "s2_transport.h"
#include "s2_l2cap.h"
#include "s2_gatt.h"
#include "s2_rumble.h"
#ifdef S2_HOST_TARGET_TEST
extern uint8_t s2_test_registers[0x45000];
void *s2_test_function(uintptr_t address);
#define B80_PTR(a) (s2_test_registers+((uintptr_t)(a)-0x800000u))
#define FN(a,ret,args) ((ret (*)args)s2_test_function((a)|1u))
#else
#define B80_PTR(a) ((void *)(uintptr_t)(a))
#define FN(a,ret,args) ((ret (*)args)((a)|1u))
#endif
#define R8(a) (*(volatile uint8_t *)B80_PTR(a))
#define R16(a) (*(volatile uint16_t *)B80_PTR(a))
#define R32(a) (*(volatile uint32_t *)B80_PTR(a))
/* Linker-enforced overlays. They are valid ONLY after replacing the entire
 * BLE initializer/loop and preventing all old GATT/SMP callbacks. */
typedef struct {
    s2_session protocol;
    uint8_t peer[6],peer_type;
} s2_301b_session;
typedef struct {
    uint16_t last_ms,rx_count,tx_count,flags;
    uint8_t opcode,handle,command,subcommand;
    int8_t result;
    uint8_t error_source,stored,pending;
} s2_301b_trace;
enum { S2_TRACE_CONNECTED=1,S2_TRACE_ATT=2,S2_TRACE_SETUP=4,S2_TRACE_MTU=8,
       S2_TRACE_LTK_REQUEST=16,S2_TRACE_LTK_OK=32,S2_TRACE_BOND=64,
       S2_TRACE_INPUT=128,S2_TRACE_PHY_REQUEST=256,S2_TRACE_PHY_2M=512,
       S2_TRACE_LED_LENGTHS=1024,S2_TRACE_BUTTON=2048,S2_TRACE_REJECTED_WRITE=4096,
       S2_TRACE_REPLY_BUSY=8192,S2_TRACE_MOTOR_OUTPUT=16384,S2_TRACE_DPAD=32768 };
typedef struct {
    uint32_t last_input;
    s2_transport transport;
    s2_l2cap_rx rx;
    uint8_t cccd[8],descriptors[14];
    volatile uint8_t csa2;
    uint8_t pairing,advertise_pending;
    volatile uint8_t failed;
    uint16_t pair_started;
    uint8_t mtu_offer,wake,phy_reply_pending,led_lengths;
    s2_301b_trace trace;
} s2_301b_link;
typedef struct {
    s2_rumble_player player;
    uint16_t elapsed_ms;
    uint32_t last_tick;
    uint8_t motor[2];
} s2_301b_actuators;
extern s2_301b_session s2_unit;
extern s2_301b_link s2_link;
extern s2_301b_actuators s2_actuators;
extern s2_att_state s2_att;
void s2_301b_init(void);
void s2_301b_loop(void);
void s2_301b_pair(void);
void s2_301b_phy_init(void);
void s2_301b_phy_switch(uint8_t phy);
void s2_301b_phy_control(const uint8_t *ll);
void s2_301b_phy_poll(void);
void s2_301b_phy_instant(const uint8_t *ll);
void s2_301b_connect_packet(const uint8_t *dma);
uint8_t s2_301b_channel(volatile uint8_t *connection);
int s2_301b_flash_open(uint32_t *saved);
int s2_301b_flash_close(uint32_t saved);
#endif

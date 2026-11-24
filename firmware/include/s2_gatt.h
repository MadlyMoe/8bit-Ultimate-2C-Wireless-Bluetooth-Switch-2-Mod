/* SPDX-License-Identifier: MIT */
#ifndef S2_GATT_H
#define S2_GATT_H
#include <stddef.h>
#include <stdint.h>
#define S2_ATT_DYNAMIC 0x80u
/* Flash-only metadata for our bounded ATT server, not the stock Telink ABI. */
typedef struct {
    uint16_t group_count;
    uint8_t permissions,uuid_length;
    uint16_t value_length;
    const uint8_t *uuid,*value;
} s2_gatt_attribute;
extern const s2_gatt_attribute s2_attributes[];
/* Runtime callbacks return ATT error codes (0=success). Read reports its full
 * value length, while copying at most capacity bytes starting at offset. */
typedef struct {
    int (*read)(void *,uint16_t,uint16_t,uint8_t *,size_t,size_t *);
    int (*write)(void *,uint16_t,const uint8_t *,size_t);
    int (*send)(void *,const uint8_t *,size_t);
} s2_att_io;
/* 32 bytes. An ATT request has only one outstanding response per bearer. */
typedef struct {
    uint16_t mtu,read_handle,read_offset;
    uint8_t pending,length,opcode,data[23];
} s2_att_state;
void s2_att_reset(s2_att_state *state);
int s2_att_receive(s2_att_state *state,const s2_att_io *io,void *ctx,const uint8_t *p,size_t n);
int s2_att_poll(s2_att_state *state,const s2_att_io *io,void *ctx);
#endif

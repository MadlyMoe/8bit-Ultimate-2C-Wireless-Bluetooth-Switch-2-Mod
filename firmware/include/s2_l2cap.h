/* SPDX-License-Identifier: MIT */
#ifndef S2_L2CAP_H
#define S2_L2CAP_H
#include <stddef.h>
#include <stdint.h>
/* Largest implemented combined command is 33+8+22=63 bytes. The extra room
 * permits normal ATT metadata requests without allocating a 512-byte buffer. */
#define S2_RX_CAPACITY 88u /* largest implemented ATT command + LL header is 72 */
typedef struct { uint16_t used,expected;uint8_t bytes[S2_RX_CAPACITY]; } s2_l2cap_rx;
/* LL frame includes two-byte header. Returns 1 for a complete ATT PDU, 0 for
 * an incomplete one, negative for malformed/unsupported packets. LL control
 * and SMP frames are never forwarded into the command decoder. */
int s2_l2cap_feed(s2_l2cap_rx *rx,const uint8_t *frame,size_t length,
                  const uint8_t **att,size_t *att_length);
#endif

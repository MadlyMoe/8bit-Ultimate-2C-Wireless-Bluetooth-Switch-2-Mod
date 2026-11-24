/* SPDX-License-Identifier: MIT */
#ifndef S2_TRANSPORT_H
#define S2_TRANSPORT_H
#include "s2_protocol.h"
#define S2_HANDLE_INPUT 0x000eu
#define S2_HANDLE_COMMAND 0x0014u
#define S2_HANDLE_COMBINED 0x0016u
#define S2_HANDLE_REPLY 0x001au
#define S2_HANDLE_COMBINED_REPLY 0x001eu
enum { S2_BUSY=-7, S2_LINK=-8, S2_MTU=-9 };
/* notify must COPY bytes on acceptance. S2_BUSY keeps the exact reply pending.
 * All calls must execute on the same main-loop context, never concurrently. */
typedef int (*s2_notify)(void *ctx,uint16_t handle,const uint8_t *bytes,size_t len);
typedef struct {
    uint8_t command, sequence, sub, combined, kind, length, payload[17];
} s2_queued_reply;
typedef struct {
    s2_session *session;
    s2_notify notify;
    void *ctx;
    uint16_t mtu;
    uint8_t connected, encrypted, subscriptions, counter, head, count;
    s2_queued_reply queue[2];
} s2_transport;
/* Flash reads are immutable virtual data. They are expanded on enqueue retry;
 * commands with side effects execute exactly once, before queuing their ACK. */
void s2_transport_init(s2_transport *t,s2_session *s,s2_notify notify,void *ctx);
void s2_transport_connect(s2_transport *t);
void s2_transport_disconnect(s2_transport *t);
int s2_transport_subscribe(s2_transport *t,uint16_t cccd,const uint8_t *data,size_t len);
int s2_transport_command(s2_transport *t,uint16_t handle,const uint8_t *data,size_t len);
int s2_transport_poll(s2_transport *t,const s2_input *input);
#endif

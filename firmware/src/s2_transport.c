/* SPDX-License-Identifier: MIT */
#include "s2_transport.h"
#include <string.h>
enum { INLINE=0, FLASH=1, CONSTANT=2, ZERO=3 };
static uint8_t subscription(uint16_t handle) {
    if(handle==S2_HANDLE_INPUT) return 1;
    if(handle==S2_HANDLE_REPLY) return 2;
    if(handle==S2_HANDLE_COMBINED_REPLY) return 4;
    return 0;
}
void s2_transport_init(s2_transport *t,s2_session *s,s2_notify notify,void *ctx) {
    memset(t,0,sizeof(*t));t->session=s;t->notify=notify;t->ctx=ctx;t->mtu=23;
}
void s2_transport_disconnect(s2_transport *t) {
    t->connected=t->encrypted=t->subscriptions=t->counter=t->head=t->count=0;
    t->mtu=23;memset(t->queue,0,sizeof(t->queue));s2_disconnect(t->session);
}
void s2_transport_connect(s2_transport *t) {
    s2_transport_disconnect(t);t->connected=1;
}
int s2_transport_subscribe(s2_transport *t,uint16_t cccd,const uint8_t *data,size_t len) {
    uint8_t bit=subscription((uint16_t)(cccd-1));
    if(!t->connected) return S2_LINK;
    if(!bit) return S2_UNSUPPORTED;
    if(!data || len!=2 || data[1] || data[0]>1) return S2_INVALID;
    if(data[0]) t->subscriptions|=bit;
    else t->subscriptions&=(uint8_t)~bit;
    return S2_OK;
}
static __attribute__((noinline)) int queue_memory(s2_transport *t,int combined,const uint8_t *data,size_t len) {
    uint8_t frame[S2_MAX_REPLY];size_t n,offset=combined?14u:0u;
    s2_queued_reply *q;int rc=s2_command(t->session,combined,data,len,frame,sizeof(frame),&n);
    if(rc!=S2_OK) return rc;
    q=&t->queue[(t->head+t->count)&1u];memset(q,0,sizeof(*q));
    q->command=frame[offset];q->sequence=frame[offset+2];q->sub=frame[offset+3];
    q->combined=(uint8_t)combined;q->length=(uint8_t)(n-offset-8u);q->kind=FLASH;
    memcpy(q->payload,frame+offset+8,8);++t->count;return S2_OK;
}
int s2_transport_command(s2_transport *t,uint16_t handle,const uint8_t *data,size_t len) {
    uint8_t frame[51];
    s2_queued_reply *q;
    size_t n,offset,constant_length,i;
    int rc,combined=(handle==S2_HANDLE_COMBINED);
    uint16_t response=combined?S2_HANDLE_COMBINED_REPLY:S2_HANDLE_REPLY;
    if(!t->connected) return S2_LINK;
    if(handle!=S2_HANDLE_COMMAND && !combined) return S2_UNSUPPORTED;
    if(!(t->subscriptions&subscription(response))) return S2_LINK;
    /* Check BEFORE executing persistence or any hardware operation. */
    if(t->count==2) return S2_BUSY;
    offset=combined?33u:0u;
    if(data && len>=offset+8u && data[offset]==2 && data[offset+3]==4)
        return queue_memory(t,combined,data,len);
    rc=s2_command(t->session,combined,data,len,frame,sizeof(frame),&n);
    if(rc!=S2_OK) return rc;
    offset=combined?14u:0u;
    q=&t->queue[(t->head+t->count)&1u];memset(q,0,sizeof(*q));
    q->command=frame[offset];q->sequence=frame[offset+2];q->sub=frame[offset+3];
    q->combined=(uint8_t)combined;q->length=(uint8_t)(n-offset-8u);
    if(s2_constant_reply(q->command,q->sub,&constant_length)) {
        q->kind=CONSTANT;
    } else if(q->length<=sizeof(q->payload)) {
        q->kind=INLINE;memcpy(q->payload,frame+offset+8,q->length);
    } else {
        for(i=offset+8;i<n;++i) if(frame[i]) return S2_CAPACITY;
        q->kind=ZERO;
    }
    ++t->count;return S2_OK;
}
static int expand(s2_transport *t,const s2_queued_reply *q,uint8_t *frame,size_t *length) {
    size_t offset=q->combined?14u:0u,n=0;
    uint8_t *p=frame+offset+8;
    const uint8_t *constant;
    memset(frame,0,S2_MAX_REPLY);
    frame[offset]=q->command;frame[offset+1]=1;frame[offset+2]=q->sequence;
    frame[offset+3]=q->sub;frame[offset+4]=0x10;frame[offset+5]=0x78;
    if(q->kind==FLASH) {
        uint32_t a=(uint32_t)q->payload[4]|((uint32_t)q->payload[5]<<8)|
                   ((uint32_t)q->payload[6]<<16)|((uint32_t)q->payload[7]<<24);
        memcpy(p,q->payload,8);
        if(!t->session->read_virtual(t->session->ctx,a,p+8,q->payload[0])) return S2_BACKEND;
    } else if(q->kind==CONSTANT) {
        constant=s2_constant_reply(q->command,q->sub,&n);
        if(!constant || n!=q->length) return S2_INVALID;
        memcpy(p,constant,n);
    } else if(q->kind==INLINE) memcpy(p,q->payload,q->length);
    *length=offset+8u+q->length;return S2_OK;
}
int s2_transport_poll(s2_transport *t,const s2_input *input) {
    uint8_t frame[S2_MAX_REPLY];size_t length;int rc;
    if(!t->connected || !t->notify) return S2_LINK;
    if(t->count) {
        s2_queued_reply *q=&t->queue[t->head];
        uint16_t handle=q->combined?S2_HANDLE_COMBINED_REPLY:S2_HANDLE_REPLY;
        if(!(t->subscriptions&subscription(handle))) return S2_LINK;
        if(t->mtu<3u+8u+q->length+(q->combined?14u:0u)) return S2_MTU;
        rc=expand(t,q,frame,&length);if(rc!=S2_OK) return rc;
        rc=t->notify(t->ctx,handle,frame,length);
        if(rc==S2_OK) { memset(q,0,sizeof(*q));t->head^=1;--t->count; }
        return rc;
    }
    if(!input || !t->encrypted || !(t->subscriptions&1)) return S2_LINK;
    if(t->mtu<S2_REPORT_SIZE+3) return S2_MTU;
    s2_report(frame,input,t->counter);
    frame[11]=(t->session->features&S2_FEATURE_RUMBLE)?0x38:0x30;
    rc=t->notify(t->ctx,S2_HANDLE_INPUT,frame,S2_REPORT_SIZE);
    if(rc==S2_OK) ++t->counter;
    return rc;
}

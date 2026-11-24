/* SPDX-License-Identifier: MIT
 * ATT server for the fixed Pro2 database. Replies use bounded storage even
 * when the peer negotiates MTU 512. Discovery may legally return fewer entries
 * than fit the MTU; the client resumes at the last returned handle + 1.
 */
#include "s2_gatt.h"
#include "s2_transport.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static int type(const s2_gatt_attribute *a,uint16_t uuid) {
    return a->uuid_length==2 && u16(a->uuid)==uuid;
}
void s2_att_reset(s2_att_state *s) { memset(s,0,sizeof(*s));s->mtu=23; }
static int error(s2_att_state *s,uint8_t opcode,uint16_t handle,uint8_t code) {
    s->data[0]=1;s->data[1]=opcode;put16(s->data+2,handle);s->data[4]=code;
    s->length=5;s->pending=1;return S2_OK;
}
static int value(const s2_att_io *io,void *ctx,uint16_t h,uint16_t offset,
                 uint8_t *out,size_t capacity,size_t *length) {
    const s2_gatt_attribute *a;
    size_t size;
    if(!h || h>s2_attributes[0].group_count) return 1;
    a=&s2_attributes[h];
    if(!(a->permissions&1)) return 2;
    if(a->permissions&S2_ATT_DYNAMIC)
        return io->read?io->read(ctx,h,offset,out,capacity,length):14;
    size=a->value_length;
    if(type(a,0x2803)) { /* Telink-style one-byte property becomes full declaration. */
        const s2_gatt_attribute *next=&s2_attributes[h+1];
        uint8_t declaration[19];
        if(h==s2_attributes[0].group_count || (next->uuid_length!=2 && next->uuid_length!=16)) return 14;
        size=3u+next->uuid_length;
        declaration[0]=a->value[0];put16(declaration+1,(uint16_t)(h+1));
        memcpy(declaration+3,next->uuid,next->uuid_length);
        if(offset>size) return 7;
        *length=size;size-=offset;if(size>capacity) size=capacity;
        memcpy(out,declaration+offset,size);return 0;
    }
    if(offset>size) return 7;
    *length=size;size-=offset;if(size>capacity) size=capacity;
    memcpy(out,a->value+offset,size);return 0;
}
static __attribute__((noinline)) int discover(s2_att_state *,const s2_att_io *,void *,const uint8_t *,size_t);
int s2_att_receive(s2_att_state *s,const s2_att_io *io,void *ctx,const uint8_t *p,size_t n) {
    uint16_t h,last=s2_attributes[0].group_count;
    uint8_t op;
    if(!p || !n || !io) return S2_INVALID;
    op=p[0];
    if(op==0x1e || (op&1u)) return S2_OK; /* indication confirmation / peer response */
    /* Commands have no ATT response and may arrive while a request's response
     * waits for TX space. They must not overwrite or block that response. */
    if(s->pending && op!=0x52) return S2_BUSY;
    if(n>s->mtu) return op==0x52?S2_OK:error(s,op,0,4);
    if(op==2) {
        uint16_t mtu;
        if(n!=3 || u16(p+1)<23) return error(s,op,0,4);
        mtu=u16(p+1);s->mtu=mtu>512?512:mtu;
        s->data[0]=3;put16(s->data+1,512);s->length=3;s->pending=1;return 0;
    }
    if(op==0x0a || op==0x0c) {
        if(n!=(op==0x0a?3u:5u)) return error(s,op,0,4);
        h=u16(p+1);
        if(!h || h>last) return error(s,op,h,1);
        s->read_handle=h;s->read_offset=op==0x0c?u16(p+3):0;
        s->opcode=op;s->pending=2;return 0;
    }
    if(op==0x12 || op==0x52) {
        int rc;
        if(n<3) return op==0x52?S2_OK:error(s,op,0,4);
        h=u16(p+1);
        if(!h || h>last) rc=1;
        else if(!(s2_attributes[h].permissions&2)) rc=3;
        else rc=io->write?io->write(ctx,h,p+3,n-3):3;
        /* Bluetooth Core, Vol 3 Part F 3.4.5.3: if an ATT Write Command
         * cannot be applied, ignore it. It never produces an ATT response. */
        if(op==0x52) return S2_OK;
        if(rc) return error(s,op,h,(uint8_t)rc);
        s->data[0]=0x13;s->length=1;s->pending=1;return 0;
    }
    return discover(s,io,ctx,p,n);
}
/* Keep discovery scratch off the pairing/AES callback's call chain. */
static __attribute__((noinline)) int discover(s2_att_state *s,const s2_att_io *io,void *ctx,const uint8_t *p,size_t n) {
    uint16_t start,end,h,last=s2_attributes[0].group_count;uint8_t op=p[0];
    if(op!=4 && op!=6 && op!=8 && op!=0x10) return error(s,op,n>=3?u16(p+1):0,6);
    if(n<5) return error(s,op,0,4);
    start=u16(p+1);end=u16(p+3);
    if(!start || start>end) return error(s,op,start,1);
    if(end>last) end=last;
    if(op==4) {
        unsigned length=2,uuid_length=0;
        if(n!=5) return error(s,op,start,4);
        s->data[0]=5;
        for(h=start;h<=end;++h) {
            const s2_gatt_attribute *a=&s2_attributes[h];
            if(!uuid_length) { uuid_length=a->uuid_length;s->data[1]=(uint8_t)(uuid_length==2?1:2); }
            if(a->uuid_length!=uuid_length || length+2u+uuid_length>sizeof(s->data)) break;
            put16(s->data+length,h);memcpy(s->data+length+2,a->uuid,uuid_length);
            length+=2u+uuid_length;
        }
        if(length==2) return error(s,op,start,10);
        s->length=(uint8_t)length;s->pending=1;return 0;
    }
    if(op==6) {
        unsigned length=1;
        if(n<7) return error(s,op,start,4);
        s->data[0]=7;
        for(h=start;h<=end;++h) {
            const s2_gatt_attribute *a=&s2_attributes[h];
            if(!type(a,u16(p+5)) || a->value_length!=n-7 ||
               (a->permissions&S2_ATT_DYNAMIC) || memcmp(a->value,p+7,n-7)) continue;
            if(length+4>sizeof(s->data)) break;
            put16(s->data+length,h);
            put16(s->data+length+2,(uint16_t)(h+a->group_count-1));length+=4;
        }
        if(length==1) return error(s,op,start,10);
        s->length=(uint8_t)length;s->pending=1;return 0;
    }
    if(n!=7 && n!=21) return error(s,op,start,4);
    if(op==0x10 && (n!=7 || (u16(p+5)!=0x2800 && u16(p+5)!=0x2801))) return error(s,op,start,16);
    {
        unsigned length=2,entry=0,header=op==8?2u:4u;
        s->data[0]=(uint8_t)(op+1);
        for(h=start;h<=end;++h) {
            const s2_gatt_attribute *a=&s2_attributes[h];
            uint8_t data[19];size_t size=0;int rc;
            if(a->uuid_length!=n-5 || memcmp(a->uuid,p+5,n-5)) continue;
            rc=value(io,ctx,h,0,data,sizeof(data),&size);
            if(rc) { if(length==2) return error(s,op,h,(uint8_t)rc);break; }
            if(size>sizeof(s->data)-2u-header) size=sizeof(s->data)-2u-header;
            if(!entry) entry=(unsigned)size+header;
            if(entry!=size+header || length+entry>sizeof(s->data)) break;
            put16(s->data+length,h);
            if(header==4) put16(s->data+length+2,(uint16_t)(h+a->group_count-1));
            memcpy(s->data+length+header,data,size);length+=entry;
        }
        if(length==2) return error(s,op,start,10);
        s->data[1]=(uint8_t)entry;s->length=(uint8_t)length;s->pending=1;return 0;
    }
}
int s2_att_poll(s2_att_state *s,const s2_att_io *io,void *ctx) {
    int rc;
    if(!s->pending) return S2_OK;
    if(!io->send) return S2_BACKEND;
    if(s->pending==2) {
        uint8_t out[64];size_t length=0,capacity=s->mtu-1u;
        if(capacity>63) capacity=63;
        rc=value(io,ctx,s->read_handle,s->read_offset,out+1,capacity,&length);
        /* Do not recursively poll: that doubles the read scratch while the
         * stock encrypted transmit path is using its own sizeable frame. */
        if(rc) {
            error(s,s->opcode,s->read_handle,(uint8_t)rc);
            rc=io->send(ctx,s->data,s->length);
        } else {
            if(length<s->read_offset) return S2_INVALID;
            length-=s->read_offset;if(length>capacity) length=capacity;
            out[0]=(uint8_t)(s->opcode+1);rc=io->send(ctx,out,length+1);
        }
    } else rc=io->send(ctx,s->data,s->length);
    if(rc==S2_OK) s->pending=s->length=0;
    return rc;
}

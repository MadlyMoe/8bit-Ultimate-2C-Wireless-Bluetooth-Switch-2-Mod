/* SPDX-License-Identifier: MIT
 * Pairing, advertising and framing adapted from esp-cpp/espp (MIT),
 * commit 61630390dec81d8130956a8df899382a7c2c613d. See THIRD_PARTY.md.
 */
#include "s2_protocol.h"
#include "aes.h"
#include <string.h>

static const uint8_t b1[16] = {
    0x5c,0xf6,0xee,0x79,0x2c,0xdf,0x05,0xe1,
    0xba,0x2b,0x63,0x25,0xc4,0x1a,0x5f,0x10
};
const uint8_t *s2_constant_reply(uint8_t command,uint8_t sub,size_t *length) {
    static const uint8_t late[29]={
        1,0x20,3,0,0,0x0a,0xe8,0x1c,0x3b,0x79,0x7d,0x8b,0x3a,0x0a,0xe8,
        0x9c,0x42,0x58,0xa0,0x0b,0x42,0x0a,0xe8,0x9c,0x41,0x58,0xa0,0x0b,0x41};
    static const uint8_t other[8]={0,0,0x40,0xf0,0,0,0x60,0};
    /* Empty profile query observed after the console subscribes to input. */
    static const uint8_t profile[4]={0x61,0x12,0x50,0x0d};
    *length=0;
    if(command==1 && sub==0x0c) { *length=sizeof(profile); return profile; }
    if(command==0x11 && sub==3) { *length=sizeof(late); return late; }
    if(command==0x18 && sub==1) { *length=sizeof(other); return other; }
    return 0;
}

void s2_init(s2_session *s, const uint8_t local[6], s2_commit_bond commit,
             s2_read_virtual read_virtual, void *ctx) {
    memset(s, 0, sizeof(*s));
    memcpy(s->local_le, local, 6);
    s->commit = commit; s->read_virtual = read_virtual; s->ctx = ctx;
    s->feature_mask = S2_SUPPORTED_FEATURES;
}
void s2_disconnect(s2_session *s) {
    memset(&s->pending, 0, sizeof(s->pending));
    s->stage = s->features = 0;
    s->feature_mask = S2_SUPPORTED_FEATURES;
    if (s->hardware && s->hardware->rumble_sample && s->hardware->rumble_command)
        s->feature_mask |= S2_FEATURE_RUMBLE;
    s->led_mask = s->led_flashing = 0;
}
void s2_bind_hardware(s2_session *s, const s2_hardware *hardware) {
    s->hardware = hardware;
    s2_disconnect(s);
}
void s2_derive_ltk(const uint8_t a1[16], uint8_t ltk[16]) {
    unsigned i;
    for (i = 0; i < 16; ++i) ltk[i] = a1[i] ^ b1[i];
}
void s2_confirm(const uint8_t ltk[16], const uint8_t a2[16], uint8_t b2[16]) {
    struct AES_ctx aes;
    uint8_t key[16];
    unsigned i;
    for (i = 0; i < 16; ++i) { key[i] = ltk[15-i]; b2[i] = a2[15-i]; }
    AES_init_ctx(&aes, key);
    AES_ECB_encrypt(&aes, b2);
    /* No global scratch: safe for a serialized caller and bounded memory. */
}
int s2_advertisement(uint8_t out[S2_ADV_SIZE], const s2_bond *bond, int wake) {
    static const uint8_t base[S2_ADV_SIZE] = {
        2,1,6,27,0xff,0x53,5,1,0,3,0x7e,5,0x69,0x20,0,1,0,
        0,0,0,0,0,0,0x0f,0,0,0,0,0,0,0
    };
    if (wake && !bond) return S2_INVALID;
    memcpy(out, base, sizeof(base));
    if (bond) memcpy(out + 17, bond->host_le, 6);
    if (wake) out[16] = 0x81;
    return S2_OK;
}
static void pack_stick(uint8_t *out, uint16_t x, uint16_t y) {
    if (x > 4095) x = 4095;
    if (y > 4095) y = 4095;
    out[0] = (uint8_t)x;
    out[1] = (uint8_t)((x >> 8) | (y << 4));
    out[2] = (uint8_t)(y >> 4);
}
void s2_report(uint8_t out[S2_REPORT_SIZE], const s2_input *in, uint8_t counter) {
    unsigned pct = in->battery_percent > 100 ? 100 : in->battery_percent;
    unsigned level = (pct * 9u + 50u) / 100u;
    memset(out, 0, S2_REPORT_SIZE);
    out[0] = counter;
    out[1] = (uint8_t)((level << 2) | (in->charging ? 2u : 0u) |
                      (in->external_power ? 1u : 0u));
    out[2] = in->buttons[0]; out[3] = in->buttons[1];
    out[4] = in->buttons[2] & 0x0f; /* no nonexistent C/headset button */
    pack_stick(out + 5, in->lx, in->ly);
    pack_stick(out + 8, in->rx, in->ry);
    out[11] = 0x30;
    /* No motion, NFC, mouse or audio samples are fabricated. */
}

static int pair(s2_session *s, uint8_t sub, const uint8_t *p, size_t n,
                uint8_t *r, size_t *rn) {
    switch (sub) {
    case 1:
        if (n < 2 || p[0] != 0 || p[1] == 0 || p[1] > 2 || n < 2u + 6u*p[1])
            return S2_SHORT;
        memset(&s->pending, 0, sizeof(s->pending));
        memcpy(s->pending.host_le, p+2, 6); s->stage = 1;
        r[0]=1; r[1]=4; r[2]=1; memcpy(r+3,s->local_le,6); *rn=9;
        return S2_OK;
    case 4:
        if (n < 17 || p[0] != 0) return S2_SHORT;
        if (s->stage < 1 || s->stage > 2) return S2_SEQUENCE;
        s2_derive_ltk(p+1,s->pending.ltk_wire); s->stage=2;
        r[0]=1; memcpy(r+1,b1,16); *rn=17; return S2_OK;
    case 2:
        if (n < 17 || p[0] != 0) return S2_SHORT;
        if (s->stage < 2 || s->stage > 3) return S2_SEQUENCE;
        r[0]=1; s2_confirm(s->pending.ltk_wire,p+1,r+1);
        s->stage=3; *rn=17; return S2_OK;
    case 3:
        if (n < 1 || p[0] != 0) return S2_SHORT;
        if (s->stage != 3) return S2_SEQUENCE;
        if (!s->commit || !s->commit(s->ctx,&s->pending)) return S2_BACKEND;
        s->stage=4; r[0]=1; *rn=1; return S2_OK;
    default: return S2_UNSUPPORTED;
    }
}

int s2_command(s2_session *s, int combined, const uint8_t *in, size_t len,
               uint8_t *out, size_t capacity, size_t *written) {
    const uint8_t *p;
    uint8_t cmd,sub,*r;
    size_t n,rn=0,offset=combined?14u:0u,required;
    int rc=S2_OK;
    if (!written) return S2_INVALID;
    *written=0;
    if (!s || !in || !out) return S2_INVALID;
    if (combined) {
        if (len < 41) return S2_SHORT;
        in+=33; len-=33;
    }
    if (len < 8) return S2_SHORT;
    if (in[1] != 0x91) return S2_INVALID;
    cmd=in[0]; sub=in[3]; p=in+8; n=in[5];
    /* Player LED patterns and flashing each use one octet. Some frames can
     * omit the reserved tail of the nominal eight-byte LED field. */
    if (n > len-8) {
        if (cmd!=9 || (sub!=7 && sub!=8) || n>8 || len==8) return S2_SHORT;
        n=len-8;
    }
    /* Pairing/AES must not carry the maximum flash-read frame on its stack.
     * Reserve the command class maximum before executing any side effect. */
    required=offset+8u+29u;
    if(cmd==2 && sub==4) {
        if(n<8) return S2_SHORT;
        if(!p[0] || p[0]>S2_MAX_FLASH_READ) return S2_INVALID;
        required=offset+16u+p[0];
    }
    if(capacity<required) return S2_CAPACITY;
    memset(out,0,required); r=out+offset+8;
    if (cmd==0x15) {
        rc=pair(s,sub,p,n,r,&rn);
        if (rc!=S2_OK && rc!=S2_BACKEND) {
            memset(&s->pending,0,sizeof(s->pending)); s->stage=0;
        }
    } else if (cmd==0x02 && sub==4) {
        uint32_t addr;
        if (n < 8) return S2_SHORT;
        if (p[0] > S2_MAX_FLASH_READ || p[0] == 0) return S2_INVALID;
        addr=(uint32_t)p[4]|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
        if (addr > UINT32_MAX-p[0]) return S2_INVALID;
        if (!s->read_virtual || !s->read_virtual(s->ctx,addr,r+8,p[0])) return S2_BACKEND;
        r[0]=p[0]; memcpy(r+4,p+4,4); rn=8u+p[0];
    } else if (cmd==0x0c) {
        if (sub==1) {
            if (n < 1) return S2_SHORT;
            r[4]=(p[0]&S2_FEATURE_BUTTONS)?7:0;
            r[5]=(p[0]&S2_FEATURE_STICKS)?7:0; rn=12;
            if (s->hardware && s->hardware->rumble_sample && s->hardware->rumble_command)
                r[9]=(p[0]&S2_FEATURE_RUMBLE)?3:0;
        } else if (sub>=2 && sub<=5) {
            if (n < 1) return S2_SHORT;
            if (sub==2) {
                uint8_t supported=S2_SUPPORTED_FEATURES;
                if (s->hardware && s->hardware->rumble_sample && s->hardware->rumble_command)
                    supported|=S2_FEATURE_RUMBLE;
                s->feature_mask=p[0]&supported;
                s->features=s->feature_mask;
            } else if (sub==3) s->feature_mask&=(uint8_t)~p[0];
            else if (sub==4) s->features|=p[0]&s->feature_mask;
            else s->features&=(uint8_t)~p[0];
            s->features&=s->feature_mask; rn=4;
        } else return S2_UNSUPPORTED;
    } else if (cmd==0x09) {
        uint8_t mask=s->led_mask, flashing=s->led_flashing;
        if (sub>=1 && sub<=4) mask=(uint8_t)(1u<<(sub-1));
        else if (sub==5) mask=15;
        else if (sub==6) mask=0;
        else if (sub==7) {
            if (!n) return S2_SHORT;
            mask=p[0]&15;
        } else if (sub==8) {
            if (!n) return S2_SHORT;
            flashing=(uint8_t)(p[0]!=0);
        } else return S2_UNSUPPORTED;
        if (!s->hardware || !s->hardware->leds || !s->hardware->leds(s->ctx,mask,flashing))
            return S2_BACKEND;
        s->led_mask=mask; s->led_flashing=flashing;
    } else if (cmd==0x0b) {
        uint16_t mv=0;
        uint8_t status[4];
        if (sub==6) { r[0]=0x11; rn=4; }
        else if (sub==3 || sub==4) {
            if (!s->hardware || !s->hardware->battery || !s->hardware->battery(s->ctx,&mv,status))
                return S2_BACKEND;
            if (sub==3) { r[0]=(uint8_t)mv; r[1]=(uint8_t)(mv>>8); }
            else memcpy(r,status,4);
            rn=4;
        } else return S2_UNSUPPORTED;
    } else if (cmd==0x0a) {
        if (!s->hardware) return S2_BACKEND;
        if (sub==2) {
            if (n<4) return S2_SHORT;
            if (p[0]>7) return S2_INVALID;
            if (!s->hardware->rumble_sample || !s->hardware->rumble_sample(s->ctx,p[0]))
                return S2_BACKEND;
        } else if (sub==8) {
            if (n<20) return S2_SHORT;
            if (!s->hardware->rumble_command || !s->hardware->rumble_command(s->ctx,p))
                return S2_BACKEND;
        } else return S2_UNSUPPORTED;
    } else if (cmd==0x10 && sub==1) {
        if (!s->hardware || !s->hardware->firmware_info) return S2_BACKEND;
        memcpy(r,s->hardware->firmware_info,12); rn=12;
    } else if (cmd==0x03) {
        if(sub==7) {
            unsigned i;
            if(n!=22) return S2_SHORT;
            /* Captured reconnect exchange: both fields are byte-reversed here,
             * unlike the A1/B1 legacy LTK exchange on command 0x15. */
            for(i=0;i<6;++i) s->pending.host_le[i]=p[5-i];
            for(i=0;i<16;++i) s->pending.ltk_wire[i]=p[21-i];
            s->stage=5;
        } else if(sub==9) {
            if(n || s->stage!=5) return S2_SEQUENCE;
            if(!s->commit || !s->commit(s->ctx,&s->pending)) return S2_BACKEND;
            s->stage=4;
        } else if (!s->hardware || !s->hardware->initialize ||
                   !s->hardware->initialize(s->ctx,sub,p,n)) return S2_BACKEND;
    } else if (cmd==7 && sub==1) rn=1;
    else if (cmd==0x16 && sub==1) rn=24;
    else if (cmd==0x11 && sub==1) { r[0]=1; rn=4; }
    else {
        const uint8_t *reply=s2_constant_reply(cmd,sub,&rn);
        if(!reply) return S2_UNSUPPORTED;
        if(cmd==1 && n) return S2_INVALID;
        memcpy(r,reply,rn);
    }
    if (rc!=S2_OK) return rc;
    out[offset]=cmd; out[offset+1]=1; out[offset+2]=in[2]; out[offset+3]=sub;
    out[offset+4]=0x10; out[offset+5]=0x78;
    *written=offset+8+rn;
    return S2_OK;
}

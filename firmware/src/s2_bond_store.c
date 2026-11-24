/* SPDX-License-Identifier: MIT */
#include "s2_bond_store.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v) {
    unsigned i; for(i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t v=UINT32_MAX;
    unsigned i;
    while(n--) {
        v^=*p++;
        for(i=0;i<8;++i) v=(v>>1)^((v&1)?0xedb88320u:0);
    }
    return ~v;
}
static int erased(const uint8_t *p,size_t n) {
    while(n--) if(*p++!=255) return 0;
    return 1;
}
static int valid(const uint8_t p[S2_BOND_RECORD_SIZE]) {
    return !memcmp(p,"S2BD\1\0",6) && p[6]<=1 && p[7]==255 &&
        get32(p+40)==crc(p,40) && get32(p+44)==0x32444e42u && erased(p+48,16);
}
static void unpack(s2_saved_bond *b,const uint8_t *p) {
    memcpy(b->protocol.host_le,p+12,6); memcpy(b->protocol.ltk_wire,p+18,16);
    memcpy(b->peer_le,p+34,6); b->peer_type=p[6];
}
static int geometry(const s2_bond_io *io) {
    return io && io->read && io->program && io->erase_sector &&
        io->sector_size>=2*S2_BOND_RECORD_SIZE && io->sector_size<=0x10000u &&
        !(io->sector_size%S2_BOND_RECORD_SIZE);
}
int s2_bond_open(s2_bond_store *s,const s2_bond_io *io,s2_saved_bond *out) {
    uint8_t record[S2_BOND_RECORD_SIZE];
    uint32_t address,sequence,difference;
    if(!s || !out || !geometry(io)) return S2_INVALID;
    memset(s,0,sizeof(*s)); s->io=io; memset(out,0,sizeof(*out));
    for(address=0;address<2*io->sector_size;address+=S2_BOND_RECORD_SIZE) {
        if(!io->read(io->ctx,address,record,sizeof(record))) {
            s->io=0; s->present=0; memset(out,0,sizeof(*out)); return S2_STORE_IO;
        }
        if(!valid(record)) continue;
        sequence=get32(record+8); difference=sequence-s->sequence;
        if(!s->present || (difference && difference<0x80000000u)) {
            s->latest=address; s->sequence=sequence; s->present=1; unpack(out,record);
        }
    }
    return s->present?S2_OK:S2_STORE_EMPTY;
}
static int matches(const s2_bond_io *io,uint32_t address,const uint8_t *expected) {
    uint8_t check[16];unsigned i;
    for(i=0;i<S2_BOND_RECORD_SIZE;i+=sizeof(check))
        if(!io->read(io->ctx,address+i,check,sizeof(check)) ||
           memcmp(check,expected+i,sizeof(check))) return 0;
    return 1;
}
int s2_bond_save(s2_bond_store *s,const s2_saved_bond *bond) {
    uint8_t record[S2_BOND_RECORD_SIZE];
    uint32_t address,slot=UINT32_MAX;
    const s2_bond_io *io;
    if(!s || !bond || bond->peer_type>1 || !geometry(s->io)) return S2_INVALID;
    io=s->io;
    for(address=0;address<2*io->sector_size;address+=S2_BOND_RECORD_SIZE) {
        if(!io->read(io->ctx,address,record,sizeof(record))) return S2_STORE_IO;
        if(erased(record,sizeof(record))) { slot=address; break; }
    }
    if(slot==UINT32_MAX) return S2_STORE_FULL;
    memset(record,255,sizeof(record)); memcpy(record,"S2BD\1\0",6);
    record[6]=bond->peer_type; put32(record+8,s->sequence+1);
    memcpy(record+12,bond->protocol.host_le,6); memcpy(record+18,bond->protocol.ltk_wire,16);
    memcpy(record+34,bond->peer_le,6); put32(record+40,crc(record,40));
    /* Verify the data before programming the separate commit word. */
    if(!io->program(io->ctx,slot,record,44) ||
       !matches(io,slot,record))
        return S2_STORE_IO;
    put32(record+44,0x32444e42u);
    if(!io->program(io->ctx,slot+44,record+44,4) ||
       !matches(io,slot,record))
        return S2_STORE_IO;
    s->latest=slot; ++s->sequence; s->present=1;
    return S2_OK;
}
int s2_bond_prepare(s2_bond_store *s) {
    uint8_t record[S2_BOND_RECORD_SIZE];
    uint32_t address,start;
    unsigned sector;
    const s2_bond_io *io;
    if(!s || !geometry(s->io)) return S2_INVALID;
    io=s->io;
    /* Any free slot suffices; unnecessary erases shorten flash lifetime. */
    for(address=0;address<2*io->sector_size;address+=S2_BOND_RECORD_SIZE) {
        if(!io->read(io->ctx,address,record,sizeof(record))) return S2_STORE_IO;
        if(erased(record,sizeof(record))) return S2_OK;
    }
    sector=s->present && s->latest<io->sector_size?1u:0u;
    if(!io->erase_sector(io->ctx,sector)) return S2_STORE_IO;
    start=sector*io->sector_size;
    for(address=start;address<start+io->sector_size;address+=S2_BOND_RECORD_SIZE)
        if(!io->read(io->ctx,address,record,sizeof(record)) || !erased(record,sizeof(record)))
            return S2_STORE_IO;
    return S2_OK;
}
int s2_bond_for_peer(const s2_saved_bond *bond,uint8_t type,const uint8_t peer[6]) {
    return bond && peer && type<=1 && bond->peer_type==type && !memcmp(bond->peer_le,peer,6);
}
int s2_bond_trace_append(const s2_bond_io *io,const uint8_t payload[32]) {
    uint8_t record[S2_BOND_RECORD_SIZE];uint32_t address,slot=UINT32_MAX;
    unsigned free_slots=0;
    if(!geometry(io) || !payload) return S2_INVALID;
    for(address=0;address<2*io->sector_size;address+=S2_BOND_RECORD_SIZE) {
        if(!io->read(io->ctx,address,record,sizeof(record))) return S2_STORE_IO;
        if(!erased(record,sizeof(record))) continue;
        if(slot==UINT32_MAX) slot=address;
        if(++free_slots==3) break;
    }
    /* Reserve both an initial bond and a subsequent host key refresh. */
    if(free_slots<3) return S2_STORE_FULL;
    memset(record,255,sizeof(record));memcpy(record,"S2DG\1\0",6);
    memcpy(record+8,payload,32);put32(record+40,crc(record,40));
    if(!io->program(io->ctx,slot,record,44) || !matches(io,slot,record)) return S2_STORE_IO;
    put32(record+44,0x44474f4cu); /* LOGD: independent diagnostic commit marker */
    if(!io->program(io->ctx,slot+44,record+44,4) || !matches(io,slot,record)) return S2_STORE_IO;
    return S2_OK;
}

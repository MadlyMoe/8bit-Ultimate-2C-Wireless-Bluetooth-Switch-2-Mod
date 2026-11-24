/* SPDX-License-Identifier: MIT */
#ifndef S2_BOND_STORE_H
#define S2_BOND_STORE_H
#include "s2_protocol.h"

#define S2_BOND_RECORD_SIZE 64u
typedef struct {
    s2_bond protocol;
    uint8_t peer_le[6], peer_type;
} s2_saved_bond;
/* Offsets are relative to TWO independently erasable, equal sized sectors.
 * Backend owns the physical bounds and may not expose factory flash. */
typedef struct {
    int (*read)(void *, uint32_t, uint8_t *, size_t);
    int (*program)(void *, uint32_t, const uint8_t *, size_t);
    int (*erase_sector)(void *, unsigned);
    void *ctx;
    uint32_t sector_size;
} s2_bond_io;
typedef struct {
    const s2_bond_io *io;
    uint32_t latest, sequence;
    uint8_t present;
} s2_bond_store;
enum { S2_STORE_IO=-10, S2_STORE_FULL=-11, S2_STORE_EMPTY=1 };
/* Erased storage is valid. Corrupt/torn records are skipped, never trusted. */
int s2_bond_open(s2_bond_store *, const s2_bond_io *, s2_saved_bond *);
/* Append only: NO sector erase while a radio connection is active. */
int s2_bond_save(s2_bond_store *, const s2_saved_bond *);
/* Call only with the radio stopped. Erases the sector without the newest
 * committed record. A failure leaves that newest record untouched. */
int s2_bond_prepare(s2_bond_store *);
int s2_bond_for_peer(const s2_saved_bond *, uint8_t, const uint8_t[6]);
/* Diagnostic payload contains metadata only, never key material. Append-only,
 * no erase; reserve two erased slots for pairing and a subsequent key refresh. */
int s2_bond_trace_append(const s2_bond_io *, const uint8_t payload[32]);
#endif

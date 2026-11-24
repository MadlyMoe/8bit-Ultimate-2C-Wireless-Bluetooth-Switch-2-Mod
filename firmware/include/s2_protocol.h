/* SPDX-License-Identifier: MIT */
#ifndef S2_PROTOCOL_H
#define S2_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

/* Portable protocol core only. There is deliberately no device-address binding here. */
#define S2_REPORT_SIZE 63u
#define S2_ADV_SIZE 31u
#define S2_MAX_FLASH_READ 79u /* documented Bluetooth memory-read maximum */
#define S2_MAX_REPLY (14u + 8u + 8u + S2_MAX_FLASH_READ)
#define S2_FEATURE_BUTTONS 1u
#define S2_FEATURE_STICKS 2u
#define S2_FEATURE_RUMBLE 32u
#define S2_SUPPORTED_FEATURES (S2_FEATURE_BUTTONS | S2_FEATURE_STICKS)

typedef struct __attribute__((packed)) {
    uint8_t host_le[6];
    uint8_t ltk_wire[16]; /* A1 XOR B1; HCI key uses this order. AES proof reverses it. */
} s2_bond;

/* Success means the new bond is durably committed AND available to LL encryption.
 * Failure must leave the previous bond usable. This callback is not yet bound
 * to the stock Telink stack, and must never write factory calibration sectors. */
typedef int (*s2_commit_bond)(void *ctx, const s2_bond *bond);
/* A virtual Nintendo calibration/info address space, NOT raw 8BitDo flash. */
typedef int (*s2_read_virtual)(void *ctx, uint32_t addr, uint8_t *out, size_t len);
/* Hardware callbacks return nonzero only after the operation has been accepted.
 * Firmware-info bytes are supplied by the complete target profile, never guessed
 * from the installed 8BitDo version. The pointer must remain valid for the session. */
typedef struct {
    int (*leds)(void *ctx, uint8_t mask, uint8_t flashing);
    int (*battery)(void *ctx, uint16_t *millivolts, uint8_t status[4]);
    int (*rumble_sample)(void *ctx, uint8_t sample);
    int (*rumble_command)(void *ctx, const uint8_t data[20]);
    int (*initialize)(void *ctx, uint8_t sub, const uint8_t *data, size_t len);
    const uint8_t *firmware_info; /* exactly 12 protocol bytes */
} s2_hardware;
typedef struct {
    uint8_t local_le[6];
    uint8_t stage, features, feature_mask;
    s2_bond pending;
    s2_commit_bond commit;
    s2_read_virtual read_virtual;
    void *ctx;
    const s2_hardware *hardware;
    uint8_t led_mask, led_flashing;
} s2_session;

typedef struct {
    uint8_t buttons[3]; /* Pro2 logical bit layout; not the stock button word */
    uint16_t lx, ly, rx, ry; /* already calibrated, 0..4095 */
    uint8_t battery_percent, charging, external_power;
} s2_input;

enum { S2_OK = 0, S2_SHORT = -1, S2_UNSUPPORTED = -2,
       S2_SEQUENCE = -3, S2_BACKEND = -4, S2_CAPACITY = -5, S2_INVALID = -6 };

void s2_init(s2_session *s, const uint8_t local_le[6], s2_commit_bond commit,
             s2_read_virtual read_virtual, void *ctx);
void s2_disconnect(s2_session *s);
void s2_bind_hardware(s2_session *s, const s2_hardware *hardware);
void s2_derive_ltk(const uint8_t a1[16], uint8_t ltk_wire[16]);
void s2_confirm(const uint8_t ltk_wire[16], const uint8_t a2[16], uint8_t b2[16]);
/* Immutable late-init payload, shared with the compact response queue. */
const uint8_t *s2_constant_reply(uint8_t command, uint8_t sub, size_t *length);
int s2_advertisement(uint8_t out[S2_ADV_SIZE], const s2_bond *bond, int wake);
void s2_report(uint8_t out[S2_REPORT_SIZE], const s2_input *in, uint8_t counter);
/* combined=1: skip exactly 33 vibration bytes; prefix reply with 14 bytes.
 * The caller must serialize access. Non-memory replies need 37 bytes (51 for
 * combined); memory reads need 16 + requested length (plus 14 for combined).
 * Reserving S2_MAX_REPLY handles every command. Retain
 * successful replies until notification enqueue succeeds (do not drop on busy).
 * No success response is produced for unimplemented hardware operations. */
int s2_command(s2_session *s, int combined, const uint8_t *in, size_t len,
               uint8_t *out, size_t capacity, size_t *written);
#endif

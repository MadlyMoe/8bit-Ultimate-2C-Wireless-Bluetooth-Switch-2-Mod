/* SPDX-License-Identifier: MIT */
#ifndef S2_RUMBLE_H
#define S2_RUMBLE_H
#include <stddef.h>
#include <stdint.h>
/* ERM translation: three 5 ms amplitude samples per grip. Frequency is not
 * independently controllable on the stock eccentric-mass motors. Empty blocks
 * mean no update; a valid block of zero-amplitude samples means explicit stop. */
typedef struct {
    uint8_t count[2], sequence[2], strength[2][3]; /* strength 0..100 */
} s2_rumble;
int s2_decode_rumble(const uint8_t *packet, size_t len, s2_rumble *out);
typedef struct {
    uint16_t started[2];
    uint8_t count[2], sequence[2], valid[2], strength[2][3];
    uint16_t hold_ms[2];
} s2_rumble_player;
void s2_rumble_stop(s2_rumble_player *player);
int s2_rumble_submit(s2_rumble_player *player,const uint8_t *packet,size_t len,uint16_t ms);
/* ERM approximations of documented effect IDs, not HD-rumble waveforms. */
int s2_rumble_preset(s2_rumble_player *player,uint8_t effect,uint16_t ms);
void s2_rumble_output(s2_rumble_player *player,uint16_t ms,uint8_t output[2]);
#endif

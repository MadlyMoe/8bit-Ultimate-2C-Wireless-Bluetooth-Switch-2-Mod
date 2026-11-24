/* SPDX-License-Identifier: MIT */
#ifndef S2_INPUT_301B_H
#define S2_INPUT_301B_H
#include "s2_protocol.h"
/* The stock calibration routine produces these 16-bit axes before its HID
 * conversion truncates them to 8 bits. Preserve all 12 protocol bits. */
typedef struct {
    uint32_t buttons;
    uint16_t axes[4];
    uint8_t zl,zr;
} s2_calibrated_301b;
void s2_convert_301b(s2_input *,const s2_calibrated_301b *);
#endif

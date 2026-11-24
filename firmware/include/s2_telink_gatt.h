/* SPDX-License-Identifier: MIT */
#ifndef S2_TELINK_GATT_H
#define S2_TELINK_GATT_H
#include <stdint.h>
/* Stock v1.09 uses packed byte loads but a 24-byte, 4-aligned record. */
typedef int (*s2_att_callback)(void *packet);
typedef struct {
    uint16_t group_count;
    uint8_t permissions, uuid_length;
    uint32_t value_length;
    const uint8_t *uuid;
    const uint8_t *value;
    s2_att_callback write, read;
} s2_telink_attribute;
extern const s2_telink_attribute s2_attributes[];
/* Definitions belong to the final, verified target RAM allocation. */
extern uint8_t s2_cccd[7][2], s2_descriptors[7][2], s2_current_input[63];
extern int s2_target_attribute_write(void *packet);
extern int s2_target_input_read(void *packet);
#endif

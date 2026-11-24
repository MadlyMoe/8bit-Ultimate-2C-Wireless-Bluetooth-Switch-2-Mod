/* SPDX-License-Identifier: MIT */
#ifndef S2_VIRTUAL_FLASH_H
#define S2_VIRTUAL_FLASH_H
#include <stddef.h>
#include <stdint.h>
/* Read-only protocol address space; never exposes physical 8BitDo flash.
 * local_le identifies this unit. Physical calibration already precedes reports. */
int s2_virtual_flash_read(const uint8_t local_le[6],uint32_t address,uint8_t *out,size_t length);
#endif

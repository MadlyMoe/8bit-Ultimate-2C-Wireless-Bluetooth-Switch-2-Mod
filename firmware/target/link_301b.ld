/* Exact-image analysis link. Installation requires the separate release gate. */
SECTIONS
{
    /* VMA is the low execution alias; builder maps this to flash + 0x6000. */
    .hook.channel 0x1ce8 : { KEEP(*(.hook.channel)) }
    .hook.loop 0xa4a4 : { KEEP(*(.hook.loop)) }
    s2_stock_check_mode = 0xa496;
    .hook.after_loop 0xa4a8 : { KEEP(*(.hook.after_loop)) }
    .hook.init 0xa4bc : { KEEP(*(.hook.init)) }
    .hook.pair 0xb8bc : { KEEP(*(.hook.pair)) }
    .hook.connect_packet 0x119f4 : { KEEP(*(.hook.connect_packet)) }
    .hook.phy_instant 0x121e6 : { KEEP(*(.hook.phy_instant)) }
    .hook.phy_dispatch 0x1274e : { KEEP(*(.hook.phy_dispatch)) }
    .extension 0x19000 : {
        *(.text .text.*)
        . = ALIGN(4);
        *(.rodata .rodata.*)
    }
    ASSERT(. <= 0x24000, "Code exceeds reviewed extension reservation")
    .unassigned : { *(.data .data.* .bss .bss.* COMMON) }
    ASSERT(SIZEOF(.unassigned) == 0, "Unexpected unallocated writable data")
    .overlay.gatt 0x84313c (NOLOAD) : { *(.overlay.gatt) }
    ASSERT(SIZEOF(.overlay.gatt) <= 60, "GATT overlay overlaps TX FIFO")
    .overlay.l2cap 0x8436c0 (NOLOAD) : { *(.overlay.l2cap) }
    ASSERT(SIZEOF(.overlay.l2cap) <= 32, "L2CAP overlay exceeds old buffer")
    .overlay.att 0x843bd4 (NOLOAD) : { *(.overlay.att) }
    ASSERT(SIZEOF(.overlay.att) <= 32, "ATT overlay overlaps L2CAP state")
    .overlay.smp 0x843c04 (NOLOAD) : { *(.overlay.smp) }
    ASSERT(SIZEOF(.overlay.smp) <= 212, "SMP overlay overlaps OTA state")
    /DISCARD/ : { *(.comment .note.* .ARM.*) }
}

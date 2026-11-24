/* SPDX-License-Identifier: MIT */
    .code 16
    .section .hook.init,"ax"
    tjl s2_301b_init
    .section .hook.loop,"ax"
    tjl s2_301b_loop
    .section .hook.after_loop,"ax"
    tj s2_stock_check_mode
    .section .hook.pair,"ax"
    tloadr r3, .Lpair
    tjex r3
.Lpair: .word s2_301b_pair + 1
    .section .hook.connect_packet,"ax"
    tjl s2_301b_connect_packet
    .section .hook.channel,"ax"
    tjl s2_301b_channel
    tnop
    .section .hook.phy_dispatch,"ax"
    tjl s2_phy_dispatch
    .section .hook.phy_instant,"ax"
    tjl s2_phy_instant_dispatch

    .section .text.s2_phy_dispatch,"ax"
    .global s2_phy_dispatch
    .thumb_func
    .type s2_phy_dispatch, %function
s2_phy_dispatch:
    tcmp r1, #0x16
    tjeq .Lphy_control
    tcmp r1, #0x17
    tjeq .Lphy_control
    tcmp r1, #0x18
    tjeq .Lphy_control
    tmov r3, r1
    tsub r3, #22
    tjex lr
.Lphy_control:
    tmov r0, r5
    tjl s2_301b_phy_control
    tloadr r3, .Lrx_done
    tjex r3
    .align 2
.Lrx_done: .word 0x125ef

    .section .text.s2_phy_instant_dispatch,"ax"
    .global s2_phy_instant_dispatch
    .thumb_func
    .type s2_phy_instant_dispatch, %function
s2_phy_instant_dispatch:
    tmov r0, r5
    tjl s2_301b_phy_instant
    tmov r6, #0
    tloadr r3, .Linstant_done
    tjex r3
    .align 2
.Linstant_done: .word 0x12237

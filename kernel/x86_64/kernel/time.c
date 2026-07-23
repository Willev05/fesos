/* File: time.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../include/kernel/time.h"

static uint64_t tsc_tick_per_ms;

void tsc_timer_init() {
    //We select the second PIT channel, since it is the only one with readable low/high on bit 5.
    //0xB6 is Channel 2, lobyte/hibyte access mode, Mode 3 (square wave gen) and 16-bit binary
    outb(0x43, 0xB6);

    //Then, we need to get ~10ms at 1.193182 MHz
    uint16_t pit_count = 11931;
    //We then write to the port in low, then the high sequencially.
    outb(0x42, (uint8_t)(pit_count & 0xFF));
    outb(0x42, (uint8_t)((pit_count >> 8) & 0xFF));

    //We then make the PC speaker (conneted to channel 2) and set bit 0 to low, making it no noise. We set the gate (bit 1) to high in order to decrement PIT.
    uint8_t gate = inb(0x61);
    outb(0x61, (gate & ~0x2) | 0x1);

    uint64_t start_tsc = rdtsc();

    //We poll the output bit (bit 5) until it goes high, which means 10 ms elapsed.
    while (!(inb(0x61) & 0x20)) cpu_relax();

    uint64_t end_tsc = rdtsc();
    tsc_tick_per_ms = (end_tsc - start_tsc) / 10;
}
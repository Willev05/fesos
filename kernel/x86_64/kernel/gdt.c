/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/kernel/gdt.h"

__attribute__((aligned(8)))
segment_descriptor gdt[3];
gdtr_t gdtr;

extern void load_gdt(uint64_t gdtr);

void gdt_init() {
    //Kernel code segment
    gdt[1].limit_low = 0xffff;
    gdt[1].access_byte = 0x9a;
    gdt[1].flags = 0xaf;
    
    //Kernel data segment
    gdt[2].limit_low = 0xffff;
    gdt[2].access_byte = 0x92;
    gdt[2].flags = 0xcf;

    gdtr.size = (uint16_t)(sizeof(gdt)) - 1;
    gdtr.offset = (uint64_t)&gdt;

    //Defined in assembly
    load_gdt((uint64_t)&gdtr);
}
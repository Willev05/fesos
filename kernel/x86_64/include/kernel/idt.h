/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t kernel_cs;
    uint8_t ist;
    //Bit 0 - 3: Gate type (0xE for interrupt type)
    //Bit 4: 0
    //Bit 5 - 6: dpl, cpu privellege level
    //Bit 7: Present, must be 1 for this to be valid
    uint8_t attributes; 
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) gate_descriptor;

typedef struct {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed)) idtr_t;

void idt_init();
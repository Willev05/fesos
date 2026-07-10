/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

//The out byte function for io ports.
static inline void outb(uint16_t port, uint8_t byte) {
    __asm__ volatile ("outb %0, %1" : : "a"(byte), "Nd"(port));
}

//The in byte function for io ports.
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
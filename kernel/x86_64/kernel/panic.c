/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/kernel/panic.h"
#include "../include/drivers/serial.h"

void kernel_panic(char *msg) {
    serial_puts("\n!!!!! KERNEL PANIC !!!!!\n");
    serial_puts(msg);
    while (1) __asm__ volatile("hlt");
}
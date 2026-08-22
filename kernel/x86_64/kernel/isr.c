/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_INTERRUPT
#define CURRENT_LOG_NAME "ISR"

#include "../include/kernel/idt.h"
#include "../include/kernel/isr.h"
#include "../include/drivers/serial.h"
#include "../include/common/logging.h"
#include "../include/kernel/panic.h"

static isr_t interrupt_handler_table[256];

void handle_interrupt(interrupt_frame *int_frame) {
    if (interrupt_handler_table[int_frame->interrupt_number]) {
        LOG_D("Interrupt #%u triggered. Passing to proper handler.\n", int_frame->interrupt_number);
        interrupt_handler_table[int_frame->interrupt_number](int_frame);
        return;
    }

    LOG_E("Unhandled interrupt: %u. Cannot recover from unhandled exception.\n", int_frame->interrupt_number);
    PANIC("Unhandled interrupt %u from instruction located at %lx.", int_frame->interrupt_number, int_frame->rip);
}

void isr_register_interrupt_handler(uint8_t interrupt_num, isr_t handler) {
    interrupt_handler_table[interrupt_num] = handler;
}
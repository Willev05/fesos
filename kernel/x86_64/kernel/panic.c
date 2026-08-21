/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/kernel/panic.h"
#include "../include/common/printf.h"
#include "../include/kernel/time.h"

void kernel_panic(const char *file, int line, const char *msg, ...) {
    //Dump the interrupt frame registers.
    uint64_t rip, rsp, rflags, rbp, cr0, cr2, cr3, cr4, cr8;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %%cr8, %0" : "=r"(cr8));

    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    __asm__ volatile(
        "lea 1f(%%rip), %0 \n"
        "1:"
        : "=r"(rip)
    );

    __asm__ volatile(
        "pushfq \n"
        "pop %0"
        : "=r"(rflags)
    );

    //Dump general purpose registers.
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15;
    __asm__ volatile("mov %%rax, %0" : "=r"(rax));
    __asm__ volatile("mov %%rbx, %0" : "=r"(rbx));
    __asm__ volatile("mov %%rcx, %0" : "=r"(rcx));
    __asm__ volatile("mov %%rdx, %0" : "=r"(rdx));

    __asm__ volatile("mov %%rsi, %0" : "=r"(rsi));
    __asm__ volatile("mov %%rdi, %0" : "=r"(rdi));

    __asm__ volatile("mov %%r8, %0" : "=r"(r8));
    __asm__ volatile("mov %%r9, %0" : "=r"(r9));
    __asm__ volatile("mov %%r10, %0" : "=r"(r10));
    __asm__ volatile("mov %%r11, %0" : "=r"(r11));
    __asm__ volatile("mov %%r12, %0" : "=r"(r12));
    __asm__ volatile("mov %%r13, %0" : "=r"(r13));
    __asm__ volatile("mov %%r14, %0" : "=r"(r14));
    __asm__ volatile("mov %%r15, %0" : "=r"(r15));

    uint64_t millisecond = tsc_timer_get_ms();
    uint32_t hour = (uint32_t)(millisecond / 3600000ULL);
    uint8_t minutes = (uint8_t)((millisecond % 3600000ULL) / 60000ULL); 
    uint8_t seconds = (uint8_t)((millisecond % 60000ULL) / 1000ULL);
    uint16_t ms = (uint16_t)(millisecond % 1000ULL);

    kprintf("\n!!!!! KERNEL PANIC !!!!!\n");
    va_list args;
    va_start(args, msg);
    vkprintf(msg, args);
    va_end(args);

    kprintf("\nFile: %s, Line: %u | Uptime: %u:%u:%u.%u\n\n", file, line, hour, minutes, seconds, ms);
    
    kprintf("CRASH CONTEXT (Interrupt Frame)\n");
    kprintf("RIP: %lx\n", rip);
    kprintf("RSP: %lx\n", rsp);
    kprintf("RBP: %lx\n", rbp);
    kprintf("RFLAGS: %lx\n", rflags);
    kprintf("CR0: %lx\n", cr0);
    kprintf("CR2: %lx\n", cr2);
    kprintf("CR3: %lx\n", cr3);
    kprintf("CR4: %lx\n", cr4);
    kprintf("CR8: %lx\n", cr8);

    kprintf("\nGENERAL PURPOSE REGISTERS\n");
    kprintf("RAX: %lx\n", rax);
    kprintf("RBX: %lx\n", rbx);
    kprintf("RCX: %lx\n", rcx);
    kprintf("RDX: %lx\n", rdx);
    kprintf("RSI: %lx\n", rsi);
    kprintf("RDI: %lx\n", rdi);
    kprintf("R8: %lx\n", r8);
    kprintf("R9: %lx\n", r9);
    kprintf("R10: %lx\n", r10);
    kprintf("R11: %lx\n", r11);
    kprintf("R12: %lx\n", r12);
    kprintf("R13: %lx\n", r13);
    kprintf("R14: %lx\n", r14);
    kprintf("R15: %lx\n", r15);

    while (1) __asm__ volatile("hlt");
}
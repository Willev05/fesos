#pragma once
#include <stdint.h>

typedef struct {
    //Pushed by interrupt_handler_wrapper
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    //Pushed by the macro
    uint64_t interrupt_number;
    uint64_t error_code; //This is either the real one or our dummy 0
    //Pushed by the CPU automatically
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame;

typedef void (*isr_t)(interrupt_frame *);
void isr_register_interrupt_handler(uint8_t interrupt_num, isr_t handler);
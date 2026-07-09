#include "../include/kernel/idt.h"

__attribute__((aligned(16)))
gate_descriptor idt[256];

idtr_t idtr;

extern uint64_t interrupt_handler_wrapper_table[256];

void idt_init() {
    //Fill the table with a reference to a simple assembly wrapper for a c generic handler that will call the true handler then.
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = (uint16_t)(interrupt_handler_wrapper_table[i] & 0xFFFF); //The lowest 16 bits
        idt[i].kernel_cs = 0x08;
        idt[i].ist = 0;
        idt[i].attributes = 0x8E;
        idt[i].offset_mid = (uint16_t)((interrupt_handler_wrapper_table[i] >> 16) & 0xFFFF); //The middle 16 bits
        idt[i].offset_high = (uint32_t)((interrupt_handler_wrapper_table[i] >> 32) & 0xFFFFFFFF); //The highest 32 bits
        idt[i].reserved = 0;
    }

    idtr.size = (uint16_t)(sizeof(idt)) - 1;
    idtr.offset = (uint64_t)&idt;

    __asm__ volatile ("lidt %0": : "m"(idtr));
}
#include "../include/kernel/idt.h"
#include "../include/kernel/isr.h"
#include "../include/drivers/serial.h"
#include "../include/common/stdstr.h"
#include "../include/kernel/panic.h"

static isr_t interrupt_handler_table[256];

void handle_interrupt(interrupt_frame *int_frame) {
    static char buffer[8];
    if (interrupt_handler_table[int_frame->interrupt_number]) {
        interrupt_handler_table[int_frame->interrupt_number](int_frame);
        return;
    }

    serial_puts("Unhandled interrupt: ");
    ultoa(int_frame->interrupt_number, buffer, 8);
    serial_puts(buffer);
    serial_puts("\n");
    kernel_panic("Cannot recover from unhandled exception.");
}

void isr_register_interrupt_handler(uint8_t interrupt_num, isr_t handler) {
    interrupt_handler_table[interrupt_num] = handler;
}
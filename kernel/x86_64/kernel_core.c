#include "include/kernel/elf.h"
#include "include/kernel/boot_info.h"
#include "include/drivers/serial.h"
#include "include/memory/memory.h"
#include "include/kernel/idt.h"
#include "include/kernel/gdt.h"

uint32_t magic_number = 0xDEADC0DE;

int uninitialized_var;

void _start(boot_info *BootInfo) {
    //Verify the loader's bss and data handling
    if (magic_number == 0xDEADC0DE) {
        uninitialized_var = 1;
    } else {
        uninitialized_var = 2;
    }

    uint64_t BootInfo_p = (uint64_t)BootInfo;
    BootInfo = (boot_info*)((uint64_t)(BootInfo) + DIRECT_MAP_BASE);

    serial_init();
    idt_init();
    serial_puts("ere\n");
    gdt_init();
    serial_puts("Finished loading tables (gdt and idt)\n");

    pmm_init(BootInfo, BootInfo_p);

    serial_puts("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
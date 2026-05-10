#include "include/kernel/elf.h"
#include "include/kernel/boot_info.h"
#include "include/drivers/serial.h"
#include "include/memory/pmm.h"

uint32_t magic_number = 0xDEADC0DE;

int uninitialized_var;

void _start(boot_info *BootInfo) {
    //Verify the loader's bss and data handling
    if (magic_number == 0xDEADC0DE) {
        uninitialized_var = 1;
    } else {
        uninitialized_var = 2;
    }

    pmm_init(BootInfo);

    serial_init();
    serial_puts("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
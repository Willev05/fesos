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
    gdt_init();
    serial_puts("Finished loading tables (gdt and idt)\n");

    //We clear the identity map since we moved our GDT. Having this map can cause next inits to incorrectly succeed.
    page_table *PML4 = (page_table*)(BootInfo->PML4 + DIRECT_MAP_BASE);
    PML4->entries[0].bits.present = 0;
    uint64_t identity_PDPT_p = PML4->entries[0].bits.physical_address << 12;

    pmm_init((uint64_t)BootInfo, BootInfo_p, identity_PDPT_p);
    vmm_init((uint64_t)BootInfo);
    vma_init();

    serial_puts("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
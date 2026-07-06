#include "include/kernel/elf.h"
#include "include/kernel/boot_info.h"
#include "include/drivers/serial.h"
#include "include/memory/memory.h"
#include "include/kernel/idt.h"
#include "include/kernel/gdt.h"
#include "include/kernel/isr.h"
#include "include/common/stdtypes.h"
#include "include/memory/kmalloc.h"

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
    //Wipe the TLB to make sure the identity map is invalidated.
    uint64_t cr3_val;
    __asm__ volatile(
    "mov %%cr3, %0\n\t"
    "mov %0, %%cr3"
    : "=r"(cr3_val)
    :
    : "memory");

    pmm_init((uint64_t)BootInfo, BootInfo_p, identity_PDPT_p);
    vmm_init((uint64_t)BootInfo);
    vma_init();
    isr_register_interrupt_handler(14, vmm_page_fault_callback);

    serial_puts("Finished memory manager init.\n");

    kmalloc_init();

    serial_puts("Finished kmalloc init.\n");
    
    volatile uint64_t *massive_integer = kmalloc(sizeof(uint64_t));
    *massive_integer = 502;

    volatile uint64_t *another_massive_integer = kmalloc(sizeof(uint64_t));
    *another_massive_integer = 441;

    volatile boot_info *bf2 = kmalloc(sizeof(boot_info));
    bf2->kernel_size = 0x6435;

    volatile uint64_t *page_int = kmalloc(2798);
    *page_int = 8321897;

    serial_puts("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
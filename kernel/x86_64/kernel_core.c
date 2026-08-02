/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "include/kernel/elf.h"
#include "include/kernel/boot_info.h"
#include "include/drivers/serial.h"
#include "include/memory/memory.h"
#include "include/kernel/idt.h"
#include "include/kernel/gdt.h"
#include "include/kernel/isr.h"
#include "include/common/stdtypes.h"
#include "include/memory/kmalloc.h"
#include "include/common/printf.h"
#include "include/kernel/time.h"

#include "include/buses/pci.h"
#include "include/drivers/storage/ahci.h"

uint32_t magic_number = 0xDEADC0DE;

int uninitialized_var;

void _start(boot_info *BootInfo) {
    //Verify the loader's bss and data handling
    if (magic_number == 0xDEADC0DE) {
        uninitialized_var = 1;
    } else {
        uninitialized_var = 2;
    }

    BootInfo = (boot_info*)((uint64_t)(BootInfo) + DIRECT_MAP_BASE);

    serial_init();
    idt_init();
    gdt_init();
    tsc_timer_init();
    kprintf("Finished loading tables (gdt and idt) and other basic systems\n");

    pmm_init((uint64_t)BootInfo);
    vmm_init((uint64_t)BootInfo);
    vma_init();
    isr_register_interrupt_handler(14, vmm_page_fault_callback);

    kprintf("Finished memory manager init.\n");

    kmalloc_init();

    kprintf("Finished kmalloc init.\n");
    
    volatile uint64_t *massive_integer = kmalloc(sizeof(uint64_t));
    *massive_integer = 502;

    volatile uint64_t *another_massive_integer = kmalloc(sizeof(uint64_t));
    *another_massive_integer = 441;

    volatile boot_info *bf2 = kmalloc(sizeof(boot_info));
    bf2->framebuffer_base = 0x6435;

    volatile uint64_t *page_int = kmalloc(2798);
    *page_int = 8321897;

    kfree(massive_integer);
    kfree(another_massive_integer);
    kfree(bf2);
    kfree(page_int);

    //test ahci
    pci_device_t ahci_cont;
    pci_find_device(0x01, 0x06, &ahci_cont);
    ahci_init_device(&ahci_cont);

    kprintf("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/elf.h>
#include <kernel/boot_info.h>
#include <drivers/serial.h>
#include <memory/memory.h>
#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <kernel/isr.h>
#include <common/stdtypes.h>
#include <kernel/time.h>
#include <common/logging.h>
#include <buses/pci.h>
#include <drivers/storage/ahci.h>
#include <kernel/drivers.h>

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

    //For debugging, change log subsystems up here.
    log_enable_subsystem_debug(LOG_SYS_NONE);

    serial_init();
    idt_init();
    gdt_init();
    tsc_timer_init();
    LOG_I("Finished loading tables (gdt and idt), serial, and tsc timers.\n");

    pmm_init((uint64_t)BootInfo);
    vmm_init((uint64_t)BootInfo);
    vma_init();
    isr_register_interrupt_handler(14, vmm_page_fault_callback);

    LOG_I("Finished memory managers init.\n");

    kmalloc_init();

    LOG_I("Finished kmalloc init.\n");
    
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

    //Kernel early driver init.
    pci_driver_t pci_driver;
    //Start by prepping the AHCI driver.
    pci_driver.name = "Generic AHCI Driver";
    pci_driver.driver_type = PCI_CLASS_DRIVER;
    pci_driver.driver_codes.class_driver.class_code = 0x01; //Mass storage
    pci_driver.driver_codes.class_driver.subclass = 0x06; //Serial ATA
    pci_driver.driver_codes.class_driver.prog_if = 0x01; //AHCI
    pci_driver.init = ahci_init_device;
    drivers_pci_register(pci_driver);

    //Then call the discover to discover PCI devices and bound early drivers.
    pci_discover();

    LOG_I("Hello from the kernel!\n");

    while (1) {
        __asm__("hlt");
    }
}
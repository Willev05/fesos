/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/memory/memory.h"
#include "../include/kernel/boot_info.h"
#include "../include/common/stdtypes.h"
#include "../include/drivers/serial.h"
#include "../include/common/stdstr.h"
#include "../include/kernel/panic.h"

static page_table *PML4; 

static page_table *vmm_walk_and_crate_next_table(page_table *table, uint16_t index);

void vmm_init(uint64_t bi_v) {
    boot_info *bi = (boot_info*)bi_v;
    PML4 = (page_table*)(bi->PML4 + DIRECT_MAP_BASE);
}

//Map this virtual address to this physical address, over n pages contiguously. Flags are defined in vmm.h.
int vmm_map(uint64_t v_addr, uint64_t p_addr, uint64_t pages, uint64_t flags) {
    if (!pages) return 0;
    for (uint64_t i = 0; i < pages; i++) {
        uint16_t PML4_index = (v_addr >> 39) & 0x1FF;
        uint16_t PDPT_index = (v_addr >> 30) & 0x1FF;
        uint16_t PD_index = (v_addr >> 21) & 0x1FF;

        page_table *PDPT = vmm_walk_and_crate_next_table(PML4, PML4_index);
        page_table *PD = vmm_walk_and_crate_next_table(PDPT, PDPT_index);
    
        //Here, we need to check if this will be a huge page or not.
        if (flags & 0x10) {
            //We assume the VMA or other caller will have the addresses alligned to the 2MB mark.
            PD->entries[PD_index].bits.present = 1;
            PD->entries[PD_index].bits.writeable = (flags & 0x1);
            PD->entries[PD_index].bits.user_available = (flags & 0x2) >> 1;
            PD->entries[PD_index].bits.write_through = (flags & 0x4) >> 2;
            PD->entries[PD_index].bits.disable_caching = (flags & 0x8) >> 3;
            PD->entries[PD_index].bits.huge_page = 1;
            PD->entries[PD_index].bits.global = (flags & 0x20) >> 5;
            PD->entries[PD_index].bits.execute_disable = (flags & 0x40) >> 6;

            PD->entries[PD_index].bits.physical_address = (p_addr >> 12) & ~0x1FFULL;
            
            __asm__ volatile ("invlpg (%0)" :: "r"(v_addr) : "memory");
            v_addr += 0x200000;
            continue;
        }
        
        page_table *PT = vmm_walk_and_crate_next_table(PD, PD_index);

        uint64_t PT_index = (v_addr >> 12) & 0x1FF;

        PT->entries[PT_index].bits.present = 1;
        PT->entries[PT_index].bits.writeable = (flags & 0x1);
        PT->entries[PT_index].bits.user_available = (flags & 0x2) >> 1;
        PT->entries[PT_index].bits.write_through = (flags & 0x4) >> 2;
        PT->entries[PT_index].bits.disable_caching = (flags & 0x8) >> 3;
        PT->entries[PT_index].bits.global = (flags & 0x20) >> 5;
        PT->entries[PT_index].bits.execute_disable = (flags & 0x40) >> 6;

        PT->entries[PT_index].bits.physical_address = (p_addr >> 12);

        __asm__ volatile ("invlpg (%0)" :: "r"(v_addr) : "memory");
        v_addr += 0x1000;
        p_addr += 0x1000;
    }
    return pages;
}

//Unmap this virtual address over n consecutive pages. //TODO: Fix issue where page table leaf will not be wiped completely, making some flags potentially effect future mappings of this virtual address.
int vmm_unmap(uint64_t v_addr, uint64_t pages) {
    if (!pages) return 0;
    for (uint64_t i = 0; i < pages; i++) {
        uint16_t PML4_index = (v_addr >> 39) & 0x1FF;
        uint16_t PDPT_index = (v_addr >> 30) & 0x1FF;
        uint16_t PD_index = (v_addr >> 21) & 0x1FF;

        //This function should be fine to use since the pages should exist.
        page_table *PDPT = vmm_walk_and_crate_next_table(PML4, PML4_index);
        page_table *PD = vmm_walk_and_crate_next_table(PDPT, PDPT_index);
    
        //Here, we need to check if this will be a huge page or not.
        if (PD->entries[PD_index].bits.huge_page) {
            PD->entries[PD_index].raw = 0ULL;
            __asm__ volatile ("invlpg (%0)" :: "r"(v_addr) : "memory");
            v_addr += 0x200000;
            continue;
        }
        
        page_table *PT = vmm_walk_and_crate_next_table(PD, PD_index);

        uint64_t PT_index = (v_addr >> 12) & 0x1FF;

        PT->entries[PT_index].raw = 0ULL;
        __asm__ volatile ("invlpg (%0)" :: "r"(v_addr) : "memory");
        v_addr += 0x1000;
    }
    return pages;
}

uint64_t vmm_get_physical_from_virtual(uint64_t v_addr) {
    //We can assume there is a valid table and that this is a valid mapped address
    uint16_t PML4_index = (v_addr >> 39) & 0x1FF;
    uint16_t PDPT_index = (v_addr >> 30) & 0x1FF;
    uint16_t PD_index = (v_addr >> 21) & 0x1FF;

    page_table *PDPT = vmm_walk_and_crate_next_table(PML4, PML4_index);
    page_table *PD = vmm_walk_and_crate_next_table(PDPT, PDPT_index);
    
    //Here, we need to check if this will be a huge page or not.
    if (PD->entries[PD_index].bits.huge_page) {
        return PD->entries[PD_index].bits.physical_address << 12;
    }
        
    page_table *PT = vmm_walk_and_crate_next_table(PD, PD_index);

    uint64_t PT_index = (v_addr >> 12) & 0x1FF;

    return PT->entries[PT_index].bits.physical_address << 12;
}

void vmm_page_fault_callback(interrupt_frame *iframe) {
    char buffer[19];

    uint64_t invalid_address = iframe->cr2;
    uint8_t is_present = iframe->error_code & 0x1;
    uint8_t is_write = iframe->error_code & 0x2;
    uint8_t is_user = iframe->error_code & 0x4;

    //Here, we will handle demand paging.
    if (!is_present) {
        //We let the vma handle it. If it returns 0, then we assume it was a valid request and return to the proper flow.
        if (!vma_demand_paging(invalid_address, is_user)) return;
        serial_puts("Demand paging returns invalid address, continuing fault handler...\n");
    }

    serial_puts("Page fault! Invalid address access at ");
    ultox(invalid_address, buffer, 19);
    serial_puts(buffer);
    serial_puts(" from instruction located at ");
    ultox(iframe->rip, buffer, 19);
    serial_puts(buffer);
    serial_puts(".\n");
    kernel_panic("Cannot recover from page fault.\n");   
}

static page_table *vmm_walk_and_crate_next_table(page_table *table, uint16_t index) {
    page_table *next_table = NULL;
    if (!table->entries[index].bits.present) {
        //Allocate a PD table
        uint64_t next_table_p = (uint64_t)pmm_allocate_frames(1, 4096);
        next_table = (page_table*)(next_table_p + DIRECT_MAP_BASE);

        table->entries[index].bits.present = 1;
        table->entries[index].bits.writeable = 1;
        table->entries[index].bits.user_available = 1;
        table->entries[index].bits.physical_address = next_table_p >> 12;
    }
    else next_table = (page_table*)((table->entries[index].bits.physical_address << 12) + DIRECT_MAP_BASE);

    return next_table;
}
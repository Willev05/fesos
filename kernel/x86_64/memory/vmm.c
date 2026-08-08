/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/memory/memory.h"
#include "../include/kernel/boot_info.h"
#include "../include/common/stdtypes.h"
#include "../include/drivers/serial.h"
#include "../include/common/stdstr.h"
#include "../include/kernel/panic.h"
#include "../include/kernel/errno.h"

static page_table *PML4; 

static page_table *vmm_walk_and_crate_next_table(page_table *table, uint16_t index);

void vmm_init(uint64_t bi_v) {
    boot_info *bi = (boot_info*)bi_v;
    PML4 = (page_table*)(bi->PML4 + DIRECT_MAP_BASE);
}

/**
 * @brief Maps a virtual address to a physical one over `pages` consecutive pages. Flags are defined in vmm.h.
 * @param v_addr The virtual address to map.
 * @param p_addr The physical address to map to.
 * @param pages The count of contiguous pages to map, starting at the addresses defined by `v_addr` and `p_addr`.
 * @param flags The flags to apply to the mappings. Flags are bitwise and defined in vmm.h.
 * @return 0 on success, -EINVAL on invalid param (pages).
 */
int vmm_map(uint64_t v_addr, uint64_t p_addr, uint64_t pages, uint64_t flags) {
    if (!pages) return -EINVAL;
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
    return 0;
}

/**
 * @brief Unmaps a virtual address and wipes the page table entry. This will not automatically free the physical page. 
 * @param v_addr The virtual address to unmap.
 * @param pages The count of contiguous pages starting at address `v_addr` to unmap.
 * @return 0 on success, -EINVAL when pages is invalid.
 */
int vmm_unmap(uint64_t v_addr, uint64_t pages) {
    if (!pages) return -EINVAL;
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
    return 0;
}

/**
 * @brief Gets the physical address from the virtual one.
 * @param v_addr The virtual address.
 * @return The physical address.
 */
uint64_t vmm_get_physical_from_virtual(uint64_t v_addr) {
    //Start by getting the indexes up to PD
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

    return (PT->entries[PT_index].bits.physical_address << 12) + (v_addr & 0xFFF);
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
        if (!vma_demand_paging(invalid_address)) return;
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

/**
 * @brief Gets the page table entry linked to a virtual address.
 * @param v_addr The virtual address to get the page table entry for.
 * @return The page table entry.
 */
page_table_entry *vmm_get_pte(uint64_t v_addr) {
    //Start by getting the indexes up to PD
    uint16_t PML4_index = (v_addr >> 39) & 0x1FF;
    uint16_t PDPT_index = (v_addr >> 30) & 0x1FF;
    uint16_t PD_index = (v_addr >> 21) & 0x1FF;

    page_table *PDPT = vmm_walk_and_crate_next_table(PML4, PML4_index);
    page_table *PD = vmm_walk_and_crate_next_table(PDPT, PDPT_index);
    
    //Here, we need to check if this will be a huge page or not.
    if (PD->entries[PD_index].bits.huge_page) {
        return &PD->entries[PD_index];
    }
        
    page_table *PT = vmm_walk_and_crate_next_table(PD, PD_index);

    uint64_t PT_index = (v_addr >> 12) & 0x1FF;

    return &PT->entries[PT_index];
}

/**
 * @brief Pins the virtual address specified to a physical frame/address over `count` bytes. Used to ensure buffer physically exists for DMA transfers or similar.
 * @param v_addr Virtual address to start pinning at.
 * @param count Count of bytes for which to pin over. Size of buffer usually.
 * @param write_access 0 for no, > 0 for yes.
 * @return 0 for success, -EFAULT for invalid address/buffer including lack of write access, -EINVAL if count is 0/invalid.
 */
int vmm_pin_pages(uint64_t v_addr, size_t count, uint8_t write_access) {
    if (v_addr < 0xFFF) return -EFAULT; //Checks for null
    if (!count) return -EINVAL; //Checks if 0 bytes were passed, which is illegal.

    uint64_t start_page = v_addr & ~0xFFFULL;
    uint64_t end_page = (start_page + count - 1) & ~0xFFFULL;

    for (uint64_t page = start_page; page <= end_page; page += 0x1000) {
        page_table_entry *pte = vmm_get_pte(page);

        //Handle demand/lazy paging.
        if (!pte->bits.present) {
            if (!vma_demand_paging(page)) return -EFAULT;
            pte = vmm_get_pte(page); //Get updated page table entry.
        }

        //Check to see if the page is even mapped as a writeable page.
        if (write_access && !pte->bits.writeable) {
            return -EFAULT;
        }
    }

    return 0;
}

/**
 * @brief Walks to the next page table and creates it if non-existent. Ex: Pass a pointer to a PDPT and an index inside it, it will return the next table, which may be brand new if it was just created.
 * @param table A pointer to the page table to start at.
 * @param index The index for the entry within `table` containing the desired next table.
 * @return A pointer to the page table specified by the `index` inside `table`.
 */
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
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/memory/kmalloc.h"
#include "../include/memory/memory.h"
#include "../include/common/math.h"
#define SMALL_BUCKET_AGGREGATES 4 //Since 1, 2, 4, 8 will get tossed with 16 byte bucket.

//16, 32, 64, 128, 256, 512, 1024.
static kmalloc_bucket_t buckets[7];

static uint8_t get_bucket_from_size(size_t size);
static void allocate_page_for_bucket(uint8_t bucket_id);

void kmalloc_init() {
    buckets[0].bucket_size = 16;
    buckets[1].bucket_size = 32;
    buckets[2].bucket_size = 64;
    buckets[3].bucket_size = 128;
    buckets[4].bucket_size = 256;
    buckets[5].bucket_size = 512;
    buckets[6].bucket_size = 1024;
}

/**
 * @brief Allocates a block of size n on the kernel heap.
 * @param size The size in bytes of the allocation.
 * @return A pointer to the allocated block.
 */
void *kmalloc(size_t size) {
    //Cannot allocate of size 0.
    if (!size) return NULL;
    uint8_t bucket_id = get_bucket_from_size(size);

    //If the bucket is 255, we need to pass the request to the vma for allocating pure pages.
    if (bucket_id == 255) {
        //Overflow-proof rounding in case very high memory is requestd. Wont ever happen (shouldn't) but yeah.
        size_t remainder = size % 4096;
        size_t rounded_size;
        if (!remainder) rounded_size = size;
        else rounded_size = size + (4096 - remainder);

        //Then simply call the vma allocator.
        void *address = vma_allocate_memory_from_ktree(rounded_size, VMA_REGULAR, PT_WRITEABLE | PT_NX | PT_GLOBAL, NULL);
        return address;
    }

    //Check to see if the bucket has any free slots.
    if (!buckets[bucket_id].free_page_list) allocate_page_for_bucket(bucket_id);

    //We will get the address of the first free slot.
    uint64_t alloc_addr = buckets[bucket_id].free_page_list->free_list;
    //Then also update the bucket's page's data.
    buckets[bucket_id].free_page_list->blocks_in_use += 1;
    buckets[bucket_id].free_page_list->free_list = *(uint64_t*)buckets[bucket_id].free_page_list->free_list;

    //We need to move the page to the full list if it is full.
    if (buckets[bucket_id].free_page_list->blocks_in_use == buckets[bucket_id].free_page_list->total_blocks) {
        kmalloc_page_descriptor_t *full_page = buckets[bucket_id].free_page_list;
        buckets[bucket_id].free_page_list = buckets[bucket_id].free_page_list->next_page_descriptor;
        if (buckets[bucket_id].free_page_list) buckets[bucket_id].free_page_list->prev_page_descriptor = NULL; //Valid since we changed what the free_page_list pointed to.

        //Now, we need to reinsert it in the full list.
        if (buckets[bucket_id].full_page_list) buckets[bucket_id].full_page_list->prev_page_descriptor = full_page;
        full_page->prev_page_descriptor = NULL;
        full_page->next_page_descriptor = buckets[bucket_id].full_page_list;
        buckets[bucket_id].full_page_list = full_page;
    }

    return (void*)alloc_addr;
}

/**
 * @brief Frees the allocated block pointed to by ptr.
 * @param ptr A pointer to the memory block to be freed.
 */
void kfree(void *ptr) {
    //We will start by getting the page descriptor for this returned pointer.
    uint64_t v_addr = (uint64_t)ptr;
    //We check wether or not the ptr passed was allocated through a page or bucket. If the ptr is page alligned, it was page allocation since buckets will NEVER return a page alligned pointer (since metadata lives there)
    if ((v_addr & 0xFFF) == 0){
        vma_free_memory_from_ktree(v_addr);
        return;
    } 

    kmalloc_page_descriptor_t *page_descriptor = (kmalloc_page_descriptor_t*)(v_addr & ~(0xFFFULL));
    
    //We then want to internally put this block back in the free list for the page.
    *(uint64_t*)v_addr = page_descriptor->free_list;
    page_descriptor->free_list = v_addr;

    //Decrement the block in use.
    page_descriptor->blocks_in_use--;

    //We wanna check if the new pointer freeing will make this page not full anymore. If so, it can be returned to the free pool on the bucket level.
    if (page_descriptor->blocks_in_use + 1 == page_descriptor->total_blocks) { 
        kmalloc_bucket_t *bucket_descriptor = &buckets[page_descriptor->bucket_index];
        //We wanna first update the list for the full pages.
        //Start by updating the previous page (can be the pointer on the bucket struct).
        if (!page_descriptor->prev_page_descriptor) bucket_descriptor->full_page_list = page_descriptor->next_page_descriptor;
        else page_descriptor->prev_page_descriptor->next_page_descriptor = page_descriptor->next_page_descriptor;
        //Then we wanna update the next page's previous page value.
        if (page_descriptor->next_page_descriptor) page_descriptor->next_page_descriptor->prev_page_descriptor = page_descriptor->prev_page_descriptor;
        
        //Then, we need to insert at the head of the free list.
        if (bucket_descriptor->free_page_list) bucket_descriptor->full_page_list->prev_page_descriptor = page_descriptor;
        page_descriptor->prev_page_descriptor = NULL;
        page_descriptor->next_page_descriptor = buckets->full_page_list;
        buckets->full_page_list = page_descriptor;
        return;
    }
    //We check if the page is now empty, if so, we cull it.
    if (!page_descriptor->blocks_in_use) {
        //Page will always be in free if it is ready to be culled.
        kmalloc_bucket_t *bucket_descriptor = &buckets[page_descriptor->bucket_index];
        //We wanna update the list for the free pages.
        //Start by updating the previous page (can be the pointer on the bucket struct).
        if (!page_descriptor->prev_page_descriptor) bucket_descriptor->free_page_list = page_descriptor->next_page_descriptor;
        else page_descriptor->prev_page_descriptor->next_page_descriptor = page_descriptor->next_page_descriptor;
        //Then we wanna update the next page's previous page value.
        if (page_descriptor->next_page_descriptor) page_descriptor->next_page_descriptor->prev_page_descriptor = page_descriptor->prev_page_descriptor;

        //Now, we can safely free this page at the vma level.
        vma_free_memory_from_ktree((uint64_t)page_descriptor);
        return;
    }
}

/**
 * @brief Maps a physical address into the kernel MMIO tree.
 * @param physical_address The physical address of the MMIO space to map.
 * @param size The size in bytes of the MMIO space to map.
 * @param mmio_flag The flag for the MMIO mapping type. Use default in most cases.
 * @return A pointer to the mapped MMIO area.
 */
void *kmap_mmio(uint64_t physical_address, size_t size, mmio_flags_t mmio_flag) {
    uint32_t vmm_flags = PT_GLOBAL | PT_WRITEABLE;

    if (mmio_flag == MMIO_DEFAULT) {
        vmm_flags |= PT_DISABLE_CACHING;
    }

    //Calculate the page offset since we need to account for the bytes in rounding down the address to page boundary. We also need it to add to vaddr in order to make caller have the same offset into the MMIO area as expected.
    uint64_t page_offset = physical_address & 0xFFFULL;
    size += page_offset;

    //We want to make sure the size is also page alligned at the upper boundary.
    size = (size + 0xFFF) & ~0xFFFULL;

    //We need to also page-align the starting address.
    physical_address &= ~0xFFFULL;

    //Now, we preapare a backing struct to inform the VMA of the physical address.
    vma_backing backing;
    backing.mmio.physical_start = physical_address;

    //Then, call the function. We need to calculate the proper offset into the initial page since the physical address may not be page alligned. We then return the proper virtual one matching the offset of physical address.
    uint8_t *virtual_base = (uint8_t*) vma_allocate_memory_from_ktree(size, VMA_HARDWARE_MMIO, vmm_flags, &backing);
    //IF null, we simply return null.
    if (!virtual_base) return NULL;

    return  (void*)(virtual_base + page_offset);
}

//Private static helper functions. 
//Assumes size is NOT 0.
static uint8_t get_bucket_from_size(size_t size) {
    //If the request is too big or small for the buckets, return the appropriate index/exit flag.
    if (size > 1024) return 255; //This will be the "page allocator required" flag. 
    if (size < 16) return 0; //The smaller request will fall into the 16 byte requests.

    //We get the leading and trailing zeros to see what bucket to stuff the request into. 
    int leading_zeros = __builtin_clzll(size);
    int trailing_zeros = __builtin_ctzll(size);

    //Calculates the bucket index that can fit the request.
    uint8_t bucket_index;
    //Check to see if it is exactly on a power of two.
    if (leading_zeros + trailing_zeros + 1 == 64) { //Ex: 00001000 -> If looking at 8 bits, this is a power of two since 4 + 3 + 1 = 8.
        bucket_index = trailing_zeros - SMALL_BUCKET_AGGREGATES;
    }
    //If not, we know that the one size must be after what the leading ones report.
    else {
        uint8_t exponent = 64 - leading_zeros; //This calculates the exponent with 2 as base, rounded up. 0101 has 1 leading 0. 4 - 1 = 3, which is 8, a rounded up power of two of the number 5.
        bucket_index = exponent - SMALL_BUCKET_AGGREGATES;
    }

    return bucket_index;
}

static void allocate_page_for_bucket(uint8_t bucket_id) {
    //We need to get a page and set it up for use in the bucket.
    kmalloc_page_descriptor_t *new_page = vma_allocate_memory_from_ktree(4096, VMA_REGULAR, PT_WRITEABLE | PT_NX | PT_GLOBAL, NULL);
    new_page->total_blocks = (4096 - MAX(buckets[bucket_id].bucket_size, sizeof(kmalloc_page_descriptor_t))) / buckets[bucket_id].bucket_size; //This ONLY works since size is 32, which handles 16 bytes perfectly. Then, the bucket size sets itself up perfectly after.

    //Now, we need to map the free_list. We can start at the proper offset right after the page header.
    uint64_t current_free_hole = (uint64_t)(new_page) + MAX(buckets[bucket_id].bucket_size, sizeof(kmalloc_page_descriptor_t));
    //And start us off with the page pointing to it. 
    new_page->free_list = current_free_hole;
    for (uint16_t i = 0; i < new_page->total_blocks - 1; i++) {
        *(uint64_t*)current_free_hole = current_free_hole + buckets[bucket_id].bucket_size;
        current_free_hole += buckets[bucket_id].bucket_size;
    }

    //Then add this page officially to the free_page_list.
    if (buckets[bucket_id].free_page_list) buckets[bucket_id].free_page_list->prev_page_descriptor = new_page; //Should ALWAYS be false since it should be only triggered when empty, but here in case.
    new_page->prev_page_descriptor = NULL;
    new_page->next_page_descriptor = buckets[bucket_id].free_page_list;
    buckets[bucket_id].free_page_list = new_page;
}
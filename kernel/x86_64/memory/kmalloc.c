/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_KALLOC
#define CURRENT_LOG_NAME "KMALLOC"

#include <memory/memory.h>
#include <common/math.h>
#include <common/logging.h>
#define SMALL_BUCKET_AGGREGATES 4 //Since 1, 2, 4, 8 will get tossed with 16 byte bucket.

//16, 32, 64, 128, 256, 512, 1024.
static kmalloc_bucket_t buckets[7];

static uint8_t get_bucket_from_size(size_t size);
static uint8_t allocate_page_for_bucket(uint8_t bucket_id);

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
    LOG_D("Received kmalloc request of size %lu, sending to bucket %u.\n", size, bucket_id);

    //If the bucket is 255, we need to pass the request to the vma for allocating pure pages.
    if (bucket_id == 255) {
        //Overflow-proof rounding in case very high memory is requestd. Wont ever happen (shouldn't) but yeah.
        size_t remainder = size % 4096;
        size_t rounded_size;
        if (!remainder) rounded_size = size;
        else rounded_size = size + (4096 - remainder);

        //Then simply call the vma allocator.
        void *address = vma_allocate_memory_from_ktree(rounded_size, VMA_REGULAR, PT_WRITEABLE | PT_NX | PT_GLOBAL, NULL);
        LOG_D("Kmalloc request exceeded 1024 bytes, turning request into page allocation. Allocated %lu bytes starting at address %lx.\n", rounded_size, address);
        return address;
    }

    //Check to see if the bucket has any free slots.
    if (!buckets[bucket_id].free_page_list){
        if (allocate_page_for_bucket(bucket_id)) {
            LOG_E("Kernel heap tree does not contain enough space to allocate a new bucket for the malloc request.\n");
            return NULL;
        }
    } 

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

    LOG_D("Kmalloc request of size %lu to bucket %u was assigned address %lx.\n", size, bucket_id, alloc_addr);
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
        LOG_D("Kfree request of address %lx was of type page allocation due to size (guessed through address being page aligned). PAssing straight to VMA free.\n", v_addr);
        vma_free_memory_from_ktree(v_addr);
        return;
    } 

    kmalloc_page_descriptor_t *page_descriptor = (kmalloc_page_descriptor_t*)(v_addr & ~(0xFFFULL));
    uint8_t bucket_index = page_descriptor->bucket_index;
    
    //We then want to internally put this block back in the free list for the page.
    *(uint64_t*)v_addr = page_descriptor->free_list;
    page_descriptor->free_list = v_addr;

    //Decrement the block in use.
    page_descriptor->blocks_in_use--;

    //We wanna check if the new pointer freeing will make this page not full anymore. If so, it can be returned to the free pool on the bucket level.
    if (page_descriptor->blocks_in_use + 1 == page_descriptor->total_blocks) { 
        kmalloc_bucket_t *bucket_descriptor = &buckets[bucket_index];
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
        LOG_D("Kfree request of address %lx on bucket %u was completed, making the hosting page not full anymore.\n", v_addr, bucket_index);
        return;
    }
    //We check if the page is now empty, if so, we cull it.
    if (!page_descriptor->blocks_in_use) {
        //Page will always be in free if it is ready to be culled.
        kmalloc_bucket_t *bucket_descriptor = &buckets[bucket_index];
        //We wanna update the list for the free pages.
        //Start by updating the previous page (can be the pointer on the bucket struct).
        if (!page_descriptor->prev_page_descriptor) bucket_descriptor->free_page_list = page_descriptor->next_page_descriptor;
        else page_descriptor->prev_page_descriptor->next_page_descriptor = page_descriptor->next_page_descriptor;
        //Then we wanna update the next page's previous page value.
        if (page_descriptor->next_page_descriptor) page_descriptor->next_page_descriptor->prev_page_descriptor = page_descriptor->prev_page_descriptor;

        //Now, we can safely free this page at the vma level.
        vma_free_memory_from_ktree((uint64_t)page_descriptor);
        LOG_D("Kfree request of address %lx on bucket %u was completed freeing the hosting page.\n", v_addr, bucket_index);
        return;
    }
    LOG_D("Kfree request of address %lx on bucket %u was completed.\n", v_addr, bucket_index);
}

/**
 * @brief Maps a physical address into the kernel MMIO tree.
 * @param physical_address The physical address of the MMIO space to map.
 * @param size The size in bytes of the MMIO space to map.
 * @param mmio_flag The flag for the MMIO mapping type. Use default in most cases.
 * @return A pointer to the mapped MMIO area.
 */
void *kmap_mmio(uint64_t physical_address, size_t size, mmio_flags_t mmio_flag) {
    uint64_t true_physical = physical_address;
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
    if (!virtual_base) {
        LOG_E("Kmap_mmio request received NULL from vma implying out of virtual mmio memory. Request for mmio p_addr %lx over %lx bytes failed.\n", true_physical, size);
        return NULL;
    }
    LOG_D("Kmap_mmio request for physical address %lx was mapped to %lx over %lu bytes with VMM flags %lx.\n", true_physical, (uint64_t)(virtual_base + page_offset), size, vmm_flags);
    return  (void*)(virtual_base + page_offset);
}

/**
 * @brief Allocates a DMA memory area for use in device drivers.
 * @param page_count The count of pages to be allocated. Will be contiguous physically and virtually.
 * @return A struct containing the virtual and physical addresses, and the page count for later use.
 */
dma_block_t kallocate_dma(size_t page_count) {
    dma_block_t block;

    block.page_count = page_count;

    uint64_t physical_address = (uint64_t)pmm_allocate_frames(page_count, 0x1000);
    block.physical_addr = physical_address;

    if (!physical_address) {
        LOG_E("Kallocate_dma request failed due to out of contiguous physical memory! Tried to allocate %lu contiguous pages.\n", page_count);
        block.virtual_addr = NULL;
        return block;
    }

    block.virtual_addr = kmap_mmio(physical_address, 4096 * page_count, MMIO_DEFAULT);
    LOG_D("If kmap_mmio call succeeded, then kallocate_dma successfully allocated %lu contiguous pages starting at physical %lx and virtual %lx.\n", page_count, (uint64_t)block.virtual_addr, block.physical_addr);
    return block;
}

/**
 * @brief Allocates a DMA memory area scattered in physical memory. Virtual memory is however contiguous. Used if physical memory fragmentation for DMA is no issue.
 * @param page_count The count of pages to allocate. Will be contiguopus virtually, scattered physically.
 * @return A struct containing the page count, virtual address, and an array of the physicall addresses, in increasing order. This means that index 0 will be v_addr + 0, index 1 is v_addr + 4096, etc.
 */
dma_scatter_block_t kallocate_scatter_dma(size_t page_count) {
    dma_scatter_block_t dma_scatter_block;
    uint32_t vmm_flags = PT_GLOBAL | PT_WRITEABLE | PT_DISABLE_CACHING;

    if (!page_count) {
        dma_scatter_block.virtual_addr = NULL;
        return dma_scatter_block;
    }
    
    //Start by making an array to store the physical addresses.
    uint64_t *physical_addresses = kmalloc(sizeof(uint64_t) * page_count);
    if (!physical_addresses) {
        LOG_E("kallocate_scatter_dma call to kmalloc for physical addresses array returned null! Could not allocate %lu scatter dma pages.\n", page_count);
        dma_scatter_block.virtual_addr = NULL;
        return dma_scatter_block;
    }

    //Next, allocate the virtual address.
    vma_backing backing;
    backing.unmanaged.tree = VMA_TREE_KMMIO;
    void *virtual_address = vma_allocate_memory_from_ktree(page_count * 0x1000, VMA_UNMANAGED_MAPPING, 0, &backing);
    if (!virtual_address) {
        LOG_E("kallocate_scatter_dma could not find a big enough virtual range in MMIO tree to handle %lu contiguous pages.\n", page_count);
        dma_scatter_block.virtual_addr = NULL;
        kfree(physical_addresses);
        return dma_scatter_block;
    }

    uint64_t current_v_address = (uint64_t)virtual_address;
    //Try to allocate the physical pages.
    for (size_t page = 0; page < page_count; page++) {
        uint64_t new_frame = (uint64_t)pmm_allocate_frames(1, 0x1000);
        if (new_frame == 0) {
            LOG_E("PMM could not find a free frame for a page in the scattered dma. Failed at page %lu / %lu.\n", page, page_count);
            dma_scatter_block.virtual_addr = NULL;
            kfree(physical_addresses);
            return dma_scatter_block;
        }
        //Map the virtual to physical addresses.
        vmm_map(current_v_address, new_frame, 1, vmm_flags);
        current_v_address += 0x1000;
        physical_addresses[page] = new_frame;
    }

    //Now, return the block.
    dma_scatter_block.page_count = page_count;
    dma_scatter_block.physical_addrs = physical_addresses;
    dma_scatter_block.virtual_addr = virtual_address;
    LOG_D("Allocated dma scatter for %lu pages starting at virtual address %lx. Physical addresses array is located at %lx.\n", page_count, (uint64_t)virtual_address, (uint64_t)physical_addresses);
    return dma_scatter_block;
}

/**
 * @brief Frees a scattered DMA memory area.
 * @param block The DMA block representing the area to free.
 */
void kfree_scatter_dma(dma_scatter_block_t block) {
    vma_free_memory_from_ktree((uint64_t)block.virtual_addr);
    //Then, free each page in a loop since physical pages are not contiguous.
    for (size_t page = 0; page < block.page_count; page++) {
        pmm_free_frames((void*)block.physical_addrs[page], 1);
    }
    kfree(block.physical_addrs);
    LOG_D("Freed dma scatter block starting at virtual address %lx over size %lu pages.\n", (uint64_t)block.virtual_addr, block.page_count);
}

/**
 * @brief Unmaps mmio and frees from the kernel memory tree.
 * @param virtual_address Pointer to the virtual memory to be unmapped.
 * @param size Size in bytes of the memory area to unmap (same as passed to map_mmio).
 */
void kunmap_mmio(void *virtual_address) {
    vma_free_memory_from_ktree((uint64_t)virtual_address);
    LOG_D("Freed mmio at virtual address %lx.\n", (uint64_t)virtual_address);
}

/**
 * @brief Free a DMA region.
 * @param block The DMA block representing the region you wish to free.
 */
void kfree_dma(dma_block_t block) {
    pmm_free_frames((void *)block.physical_addr, block.page_count);
    vma_free_memory_from_ktree((uint64_t)block.virtual_addr);
    LOG_D("Freed dma block starting at virtual %lx, physical %lx, of size %lu pages.\n", (uint64_t)block.virtual_addr, block.physical_addr, block.page_count);
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

static uint8_t allocate_page_for_bucket(uint8_t bucket_id) {
    LOG_D("Bucket %u requires new page to accommodate request. Allocating...\n", bucket_id);
    //We need to get a page and set it up for use in the bucket.
    kmalloc_page_descriptor_t *new_page = vma_allocate_memory_from_ktree(4096, VMA_REGULAR, PT_WRITEABLE | PT_NX | PT_GLOBAL, NULL);
    if (!new_page) return 1;
    new_page->total_blocks = (4096 - MAX(buckets[bucket_id].bucket_size, sizeof(kmalloc_page_descriptor_t))) / buckets[bucket_id].bucket_size; //This ONLY works since size is 32, which handles 16 bytes perfectly. Then, the bucket size sets itself up perfectly after.
    new_page->blocks_in_use = 0;
    new_page->bucket_index = bucket_id;

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
    return 0;
}
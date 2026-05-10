#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../../../bootloader/efi.h"

static uint8_t *bitmap;
static uint64_t bitmap_size;
static uint64_t maximum_address_physical;
static uint64_t total_memory;

static void set_bitmap_bit(uint64_t bit_number);
static void unset_bitmap_bit(uint64_t bit_number);

//Initialize the PMM. Includes reading the UEFI mmap to get available physical memory area and allocate the bitmap for use in other functions. MUST BE RAN FIRST.
void pmm_init(boot_info *bi) {
    //We need to add the direct mapping base since we passed the physical address to this.
    uint8_t *mmap_ptr = (uint8_t*)(bi->mmap + DIRECT_MAP_BASE);
    uint64_t mmap_size = bi->mmap_size;
    uint64_t descriptor_size = bi->descriptor_size;

    bitmap_size = bi->memory_bitmap_size;
    maximum_address_physical = bi->maximum_address_physical;
    bitmap = (uint8_t*)(bi->memory_bitmap_address + DIRECT_MAP_BASE);

    //Loop the entire memory map to find which pages can be labeled as "free"
    for (uint64_t i = 0; i < mmap_size; i += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)mmap_ptr;
        if (mmap->Type != EfiConventionalMemory &&
        mmap->Type != EfiLoaderCode &&
        mmap->Type != EfiLoaderData &&
        mmap->Type != EfiBootServicesCode &&
        mmap->Type != EfiBootServicesData &&
        mmap->Type != EfiACPIReclaimMemory
        ) continue;

        uint64_t bit_to_change = mmap->PhysicalStart >> 12;
        for (uint64_t j = 0; j < mmap->NumberOfPages; j++) unset_bitmap_bit(bit_to_change++);

        mmap_ptr += descriptor_size;
    }

    //We reset these to 1 since they are reserved until any further code says otherwise
    uint64_t bit_to_change;

    //Kernel
    bit_to_change = bi->kernel_location_physical >> 12;
    //We loop through n * 512 since the count is # of 2MB pages
    for (uint64_t i = 0; i < bi->kernel_size * 512; i++) set_bitmap_bit(bit_to_change++);

    //Kernel stack
    bit_to_change = bi->kernel_stack_location_physical >> 12;
    for (uint64_t i = 0; i < 512; i++) set_bitmap_bit(bit_to_change++);

    //UEFI Memory Map
    bit_to_change = bi->mmap >> 12;
    for (uint64_t i = 0; i < (bi->mmap_size + 4095) / 4096; i++) set_bitmap_bit(bit_to_change++);

    //The bootinfo struct
    bit_to_change = (uint64_t)bi >> 12;
    set_bitmap_bit(bit_to_change++);

    //The memory bitmap
    bit_to_change = bi->memory_bitmap_address >> 12;
    for (uint64_t i = 0; i < (bi->memory_bitmap_size + 4095) / 4096; i++) set_bitmap_bit(bit_to_change++);

    //The assembly bridge, containing our temporary GDT
    bit_to_change = bi->bridge_location >> 12;
    //We loop through n * 512 since the count is # of 2MB pages
    for (uint64_t i = 0; i < bi->bridge_size * 512; i++) set_bitmap_bit(bit_to_change++);

    //Now, all our page tables. They are wonderful since they take up exactly 1 page, therefore 1 bit.
    //The PML4 table
    set_bitmap_bit(bi->PML4 >> 12);

    //All our other tables.
    //We loop through the array! This array itself can be overwritten later since it was only used to tell the pmm where the page tables were located.
    for (uint64_t i = 0; i < bi->page_table_addresses_count; i++) set_bitmap_bit(bi->page_table_addresses[i] >> 12);
}

static void set_bitmap_bit(uint64_t bit_number) {
    uint8_t byte_to_change = bitmap[bit_number / 8];
    byte_to_change = byte_to_change | (1 << (bit_number % 8));
}

static void unset_bitmap_bit(uint64_t bit_number) {
    uint8_t byte_to_change = bitmap[bit_number / 8];
    byte_to_change = byte_to_change & ((254 << (bit_number % 8)) | ((1 << (bit_number % 8)) - 1));
}
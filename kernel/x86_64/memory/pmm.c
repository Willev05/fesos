#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/common/math.h"
#include "../../../bootloader/efi.h"
#include "../include/kernel/boot_info.h"
#include "../include/common/stdtypes.h"

static volatile uint8_t *bitmap;
static uint64_t bitmap_size;
static uint64_t maximum_address_physical;
static uint64_t total_memory;

static void set_bitmap_bit(uint64_t bit_number);
static void unset_bitmap_bit(uint64_t bit_number);
static uint8_t get_bitmap_bit(uint64_t bit_number);

//Initialize the PMM. Includes reading the UEFI mmap to get available physical memory area and allocate the bitmap for use in other functions. MUST BE RAN FIRST.
void pmm_init(uint64_t bi_v, uint64_t bi_p, uint64_t identity_PDPT_p) {
    //Cast back to BootInfo and page_table.
    boot_info *bi = (boot_info*)bi_v;
    page_table *identity_PDPT = (page_table*)(identity_PDPT_p + DIRECT_MAP_BASE);

    //We need to add the direct mapping base since we passed the physical address to this.
    uint8_t *mmap_ptr = (uint8_t*)(bi->mmap + DIRECT_MAP_BASE);
    uint64_t mmap_size = bi->mmap_size;
    uint64_t descriptor_size = bi->descriptor_size;

    bitmap_size = bi->memory_bitmap_size;
    maximum_address_physical = bi->maximum_address_physical;
    bitmap = (uint8_t*)(bi->memory_bitmap_address + DIRECT_MAP_BASE);

    //Loop the entire memory map to find which pages can be labeled as "free"
    for (uint64_t i = 0; i < mmap_size; i += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i);
        if (mmap->Type != EfiConventionalMemory &&
        mmap->Type != EfiLoaderCode &&
        mmap->Type != EfiLoaderData &&
        mmap->Type != EfiBootServicesCode &&
        mmap->Type != EfiBootServicesData &&
        mmap->Type != EfiACPIReclaimMemory
        ) continue;

        uint64_t bit_to_change = mmap->PhysicalStart >> 12;
        for (uint64_t j = 0; j < mmap->NumberOfPages; j++) unset_bitmap_bit(bit_to_change++);
    }

    //We reset these to 1 since they are reserved until any further code says otherwise
    uint64_t bit_to_change;

    //Keep the first page reserved to make NULL valid (0x0);
    set_bitmap_bit(0);

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
    bit_to_change = (uint64_t)bi_p >> 12;
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

    //All our other page tables.
    //We need to add the DIRECT_MAP_BASE to the array base to access it.
    bi->page_table_addresses = (uint64_t*)((uint64_t)bi->page_table_addresses + DIRECT_MAP_BASE);
    //We loop through the array! This array itself can be overwritten later since it was only used to tell the pmm where the page tables were located.
    for (uint64_t i = 0; i < bi->page_table_addresses_count; i++) set_bitmap_bit(bi->page_table_addresses[i] >> 12);

    //We can lastly clear out the tables from our old identity map.
    //Since it was a 2MB pages, we can simply stop at PD.
    for(uint64_t i = 0; i < 512; i++) {
        if (identity_PDPT->entries[i].raw) {
            unset_bitmap_bit(identity_PDPT->entries[i].bits.physical_address >> 12);
        }
    }
    unset_bitmap_bit(identity_PDPT_p >> 12);
}

//Need to implement allignment support.
void *pmm_allocate_frames(uint64_t count, uint64_t alignment) {
    //If count is 0, just return as cannot allocate 0 pages.
    if (!count) return NULL;
    //We want to get the allignment for the frame, which is at minimum 4096.
    if (alignment < 4096) alignment = 4096;
    //If allignment is not a multiple of 4096, it will be rounded down to the nearest multiple. Step is in pages so bits.
    uint64_t step = alignment / 4096;
    //To quickly check over 64 pages. This should work since the bitmap should be page alligned, therefore 8 byte alligned.
    uint64_t *bitmap64 = (uint64_t*)bitmap; 
    uint64_t total_bits = 8 * bitmap_size;
    uint64_t found = 0;

    //Main loop, will loop through legal "base pages" that are always alligned.
    for (uint64_t base = 0; base < total_bits; base += step){

        //We check to see if the latest step has landed us on a 64-bit boundary. If so, we can fast track by skipping the entire uint64, so 64 pages if it is all full.
        if (base % 64 == 0) { 
            if (bitmap64[base / 64] == 0xFFFFFFFFFFFFFFFF) {
                //We get the next available bit, which is after the 64 bit boundary.
                uint64_t next_available_bit = base + 64;

                //We get the remainder to see if we are alligned, if not, we go up to the bit in order to stay alligned.
                uint64_t remainder = next_available_bit % step;
                if (remainder != 0) {
                    next_available_bit += (step - remainder);
                }

                //Set base so that when the loop adds step, it lands exactly on our target
                base = next_available_bit - step;
                continue;
            }
        }

        //This loop will take our alligned value and verify that there are n consecutvie free frames.
        uint64_t found = 0;
        for (uint64_t i = 0; i < count; i++) {
            //Guard for out of physical memory bounds;
            if (base + i >= total_bits) return NULL;
            
            //Checks to see if the frame is already in use, where it will break if so.
            if (get_bitmap_bit(base + i)) break;
            found++;
        }

        //We will check if we found a consecutive pages satisfying the requirement.
        if (found == count) {
            //We then mark these as in use so they do not get reallocated.
            for (uint64_t i = 0; i < count; i++) {
                set_bitmap_bit(base + i);
            }
            return (void*)(base * 4096);
        }

    }
    //If we reach the end of the loop, then no physical memory satisfies the requirements.
    return NULL;
}

void pmm_free_frames(void *start_address, uint64_t count) {
    if (!count) return; 
    for (uint64_t i = (uint64_t)start_address / 4096; i < count; count++) {
        unset_bitmap_bit(i);
    }
}

static void set_bitmap_bit(uint64_t bit_number) {
    bitmap[bit_number / 8] = bitmap[bit_number / 8] | (1 << (bit_number % 8));
}

static void unset_bitmap_bit(uint64_t bit_number) {
    bitmap[bit_number / 8] = bitmap[bit_number / 8] & ((254 << (bit_number % 8)) | ((1 << (bit_number % 8)) - 1));
}

static uint8_t get_bitmap_bit(uint64_t bit_number) {
    return (bitmap[bit_number / 8] >> (bit_number % 8) & 0x1);
}
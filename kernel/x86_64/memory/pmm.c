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
            unset_bitmap_bit(identity_PDPT->entries[i].bits.physical_address);
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
    uint64_t bit_counter = 0;
    uint64_t found = 0;
    uint64_t base_page = 0;

    while (bit_counter < bitmap_size * 8) {
        //Start by checking if the bit counter is alligned for 64 bits, if so, we will skip the block.
        if (bit_counter % 64 == 0) {
            //If the remaining pages are >= 64 and the 64 pages are all blank, then we can include these and skip past them all. 
            if (count - found >= 64 && bitmap64[bit_counter / 64] == 0) {
                if (bit_counter % step != 0){
                    if (found) {
                        found += 64;
                        bit_counter += 64;
                        continue;
                    }
                }
                else {
                    if (!found) base_page = bit_counter;
                    found += 64;
                    bit_counter += 64;
                    continue;
                }
            }
            //If the entire block is full, we can simply skip it.
            else if (bitmap64[bit_counter / 64] == 0xFFFFFFFFFFFFFFFF) {
                bit_counter += 64;
                found = 0;
                continue;
            }
            //If found is 0, then we can also assume that there was no 0s on the previous 64 page block boundary. 
            else {
                //We get the first occurence of the 0 using a builtin function: https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html
                uint8_t bits_to_zero = __builtin_ctzll(~bitmap64[bit_counter / 64]);
                bit_counter += bits_to_zero;
                //If the 0 is not directly after previous block, then we need to reset the found counter.
                found = (bits_to_zero) ? 0 : found;
            }
        }

        //If the step is not one page, and we need to find the first page, check alignment. Round to the next allignment point, or the 64 bit boundary to do a large check.
        if (!found && step != 1) {
            uint64_t distance_to_boundary = 64 - (bit_counter % 64);
            uint64_t distance_to_step = bit_counter % step;
            bit_counter += MAX(distance_to_boundary, bit_counter % step);
            if (distance_to_boundary <= distance_to_step) continue;
        } 

        //If a free page is found, add to found, and if found was previously 0, set the base_page since we are starting a new "window".
        if (!get_bitmap_bit(bit_counter++)) {
            //Checks the allignment for the first page, if not valid, skip by <step> pages.
            if (!found) base_page = bit_counter;
            found++;
        }
        else found = 0;
        if (found == count) break;
    }

    //Set these pages as used and return the physical address.
    if (found == count) {
        for (uint64_t i = 0; i < count; i++) {
            set_bitmap_bit(base_page + i);
        }
        return (void*)(base_page * 4096);
    }
    else return NULL;
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
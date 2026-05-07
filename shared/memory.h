#include <stdint.h>
#define DIRECT_MAP_BASE 0xFFFF888000000000
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000

//This will represent all the bits in an entry for the page table.
typedef struct {
    //All bits are increasing, starting at bit 0.
    //Bits 0-8: Standart flags.
    uint64_t present : 1; //1 is present, 0 is not.
    uint64_t writeable : 1; //1 is R/W, 0 is read-only.
    uint64_t user_available : 1; //1 is user available, 0 is kernel-only.
    uint64_t write_through : 1; //1 for write-through, 0 for normal operation.
    uint64_t disable_caching : 1; //1 for disable, 0 for enable caching (normal).
    uint64_t accessed : 1; //Set to 1 when CPU read this page. Can be used for replacement algorithms.
    uint64_t dirty : 1; //Set when CPU wrote to this page. Used for swapping.
    uint64_t huge_page: 1; //1 for hue page, 0 for regular page.
    uint64_t global: 1; //Prevent the TLB flush when switching CR3. Can be used for kernel space when context-switching. 

    //Bits 9-11: Available bits
    uint64_t available_low : 3; //Ignored, can be used by OS for something.

    //Bits 12-51: Physical frame address
    //40 bits are used since the top 16 bits are ignored, and the lowest are too (4kb alligned).
    uint64_t physical_address : 40; // Stores the address of the final frame, or the frame of the next page table.

    //Bits 52-62: More available bits
    uint64_t available_high : 11; //Ignored, can be used by OS for something.

    //Bit 63: Execute disable
    uint64_t execute_disable : 1; //1 for disabling execution, 0 for allowing execution.
} __attribute__((packed)) page_table_entry_bits;

//Lets one access the bits as a single "raw" uint64.
typedef union {
    page_table_entry_bits bits;
    uint64_t raw;
} page_table_entry;

//Page table struct.
typedef struct {
    page_table_entry entries[512];
} page_table;
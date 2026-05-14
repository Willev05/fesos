#pragma once
#include <stdint.h>

//https://wiki.osdev.org/Global_Descriptor_Table
typedef struct {
    uint16_t limit_low; //Ignored in 64 bit arch
    uint16_t base_low; //Ignored in 64 bit arch
    uint8_t base_mid; //Ignored in 64 bit arch
    //Bit 0: Accessed Bit: Modified by cpu when accessed.
    //Bit 1: RW bit: If a data segment, 0 is write not allowed. If a code segment, 0 is read not allowed. Write access never allowed for code segments, read access always allowed for data segments.
    //Bit 2: DC bit: Read on wiki. 
    //Bit 3: E bit: 1 if a code segment, 0 if a data segment.
    //Bit 4: S bit: 1 if a data or code segment, 0 if a system segment (TSS, LDT)
    //Bit 5 - 6: DPL bit: from 0-3, describes the ring access level for this segment.
    //Bit 7: Present bit: Must be 1 for any valid segment.
    uint8_t access_byte;
    //Lowest 4 bits are the high bits of limit. The highest 4 bits are the actual flags.
    //Bit 4: Reserved
    //Bit 5: Long mode flag, must be 1 for a 64-bit code segment. If set DB MUST be 0.
    //Bit 6: DB size flag: If 0, describes a 16-bit one, if 1, 32-bit
    //Bit 7: Granularity flag, indicates the size the Limit value is scaled by. If clear (0), the Limit is in 1 Byte blocks (byte granularity). If set (1), the Limit is in 4 KiB blocks (page granularity).
    uint8_t flags;
    uint8_t base_high; //Ignored in 64 bit arch
} __attribute__((packed)) segment_descriptor;

typedef struct {
    uint16_t size;
    uint64_t offset;
} __attribute__ ((packed)) gdtr_t;

void gdt_init();
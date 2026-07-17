/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/common/math.h"
#include "../../../bootloader/efi.h"
#include "../include/kernel/boot_info.h"
#include "../include/common/stdtypes.h"

static volatile uint8_t *bitmap;
static uint64_t bitmap_size_bytes;
static uint64_t bitmap_size_frames;
static uint64_t memory_total_frame_count;
static uint64_t memory_used_frame_count;

static void set_bitmap_bit(uint64_t bit_number);
static void unset_bitmap_bit(uint64_t bit_number);
static uint8_t get_bitmap_bit(uint64_t bit_number);

//Initialize the PMM. Includes reading the UEFI mmap to get available physical memory area and allocate the bitmap for use in other functions. MUST BE RAN FIRST.
void pmm_init(uint64_t bi_v) {
    boot_info *bi = (boot_info*)bi_v;
    bitmap = (uint8_t*)(bi->memory_bitmap_address + DIRECT_MAP_BASE);
    bitmap_size_bytes = bi->memory_bitmap_size;
    bitmap_size_frames = bi->memory_physical_total_frames;
    memory_total_frame_count = bi->memory_physical_total_frames;
    memory_used_frame_count = bi->memory_physical_used_frames;
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

    //Main loop, will loop through legal "base pages" that are always alligned.
    for (uint64_t base = 0; base < bitmap_size_frames; base += step){

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
            if (base + i >= bitmap_size_frames) return NULL;
            
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
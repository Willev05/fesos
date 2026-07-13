/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

//The kernel size is in 2MB pages!
typedef struct {
    //Framebuffer stuff.
    uint64_t framebuffer_base;
    uint64_t framebuffer_size; //In bytes
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixels_per_scan_line;

    //Memory bitmap data.
    uint64_t memory_bitmap_address;
    uint64_t memory_bitmap_size; //In bytes
    uint64_t memory_bitmap_frame_count;
    uint64_t memory_physical_total_frames;
    uint64_t memory_physical_used_frames;

    uint64_t kernel_location_physical;
    uint64_t kernel_location_virtual;
    uint64_t kernel_size; //In 2MB pages
    uint64_t kernel_stack_location_physical;
    uint64_t bridge_location;
    uint8_t bridge_size; //In 2MB pages
    uint64_t PML4;
    uint64_t *page_table_addresses;
    uint64_t page_table_addresses_count;
} boot_info;
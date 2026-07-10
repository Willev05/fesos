/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "../common/stdtypes.h"

typedef struct _kmalloc_page_descriptor {
    //The index into the buckets array.
    uint8_t bucket_index;
    //The count of blocks in use in the page.
    uint16_t blocks_in_use;
    //The count of blocks in use in the page.
    uint16_t total_blocks;
    //A pointer to the first free hole.
    uint64_t free_list;
    //A pointer the the next page descriptor of this bucket type for the either full or free lists.
    struct _kmalloc_page_descriptor *next_page_descriptor;
    struct _kmalloc_page_descriptor *prev_page_descriptor;

} kmalloc_page_descriptor_t;

typedef struct {
    //The power of two.
    uint32_t bucket_size;
    //A pointer to the linked list for pages containing free slots, or completely full ones. Seperated for performance.
    kmalloc_page_descriptor_t *free_page_list;
    kmalloc_page_descriptor_t *full_page_list;
} kmalloc_bucket_t;

void kmalloc_init();
void *kmalloc(size_t size);
void kfree(void *ptr);

void *kzalloc(size_t size);
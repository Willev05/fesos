/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

//Type of node in vm_ds_node.
typedef enum {
    VMA_FREE,
    VMA_REGULAR,
    VMA_FILE_BACKED,
    VMA_HARDWARE_MMIO
} __attribute__((packed)) vm_node_type;

typedef union {
    struct {
            uint64_t file_ptr; //TODO: Fill when vfs is implemented.
            uint64_t offset;
        } file;

        struct {
            uint64_t physical_start; //For MMIO mapping, since it will be physically continuous. Regular mapping will be reversed from walking page tables.
        } mmio;
} vma_backing;

//The node in the AVL tree used for the VMA
typedef struct _vm_ds_node {
    //Useful data in the node. start_addr is used for the ordering.
    uint64_t start_addr;
    uint64_t size;
    vm_node_type type;
    uint32_t flags;

    //Backing information. Depends on type.
    vma_backing backing;

    //Pointers to the other nodes in the tree.
    struct _vm_ds_node *parent;
    struct _vm_ds_node *left;
    struct _vm_ds_node *right;

    //Used for quickly finding slot with worst fit.
    uint64_t subtree_max_free_slot;
    //Used for quick balance check calc by keeping the subtree max depth.
    uint64_t subtree_max_depth;
} vm_ds_node;

void vma_init();
void *vma_allocate_memory_from_ktree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
void *vma_allocate_memory_from_utree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
void vma_free_memory_from_ktree(uint64_t start_addr);
void vma_free_memory_from_utree(uint64_t start_addr);
uint8_t vma_demand_paging(uint64_t fault_addr, uint8_t is_user);
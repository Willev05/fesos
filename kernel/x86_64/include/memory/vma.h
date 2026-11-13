/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>
#include <common/ds/avl.h>

//Type of node in vm_ds_node.
typedef enum {
    VMA_FREE, //Should not be in alloc request.
    VMA_REGULAR, //Regular user memory. Lifecycle handled by VMA. Supports demand paging.
    VMA_FILE_BACKED, //File backed memory. Lifecylce handled by VMA. Supports demand paging.
    VMA_HARDWARE_MMIO, //Memory Mapped IO. Lifecycle handled by VMA. No demand paging, pinned ASAP. PMM will not be used, physical address assumed to be blocked in bitmap.
    VMA_UNMANAGED_MAPPING //Unmanaged mapping for memory. VMA will simply reserve the required virtual memory block in it's tree. VMM and PMM will not be contacted by VMA. Used when lifecycle, and more granular control over mappings are required.
} __attribute__((packed)) vm_node_type;

typedef enum {
    VMA_TREE_KHEAP,
    VMA_TREE_KMMIO,
    VMA_TREE_USER
} vma_unmanaged_tree_t;

typedef union {
    struct {
        uint64_t file_ptr; //TODO: Fill when vfs is implemented.
        uint64_t offset;
    } file;

    struct {
        uint64_t physical_start; //For MMIO mapping, since it will be physically continuous. Regular mapping will be reversed from walking page tables.
    } mmio;

    struct {
        vma_unmanaged_tree_t tree;
    } unmanaged;
} vma_backing;

//The node in the AVL tree used for the VMA
typedef struct _vm_ds_node {
    //AVL "superclass"
    avl_node_t avl;

    //Useful data in the node. start_addr is used for the ordering.
    uint64_t start_addr;
    uint64_t size;
    vm_node_type type;
    uint32_t flags;

    //Backing information. Depends on type.
    vma_backing backing;

    //Used for quickly finding slot with worst fit.
    uint64_t subtree_max_free_slot;
} vm_ds_node;

void vma_init();
void *vma_allocate_memory_from_ktree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
void *vma_allocate_memory_from_utree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
void vma_free_memory_from_ktree(uint64_t start_addr);
void vma_free_memory_from_utree(uint64_t start_addr);
uint8_t vma_demand_paging(uint64_t fault_addr);
uint8_t vma_check_ktree(uint64_t v_addr);
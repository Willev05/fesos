#pragma once

#include <stdint.h>

//Type of node in vm_ds_node.
typedef enum {
    FREE,
    IN_USE,
    UNAVAILABLE
} __attribute__((packed)) vm_node_type;

//The node in the AVL tree used for the VMA
typedef struct _vm_ds_node {
    //Useful data in the node. start_addr is used for the ordering.
    uint64_t start_addr;
    uint64_t size;
    vm_node_type type;

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
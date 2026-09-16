/* File: avl.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include <common/stdtypes.h>

struct _avl_node_t;
/**
 * @brief The compare function that the avl operations will use. Compares 2 nodes. Hint: Case the nodes to the "subclass" struct to access the data fields. AVL_NODE_T MUST BE THE TOP MEMBER OF THE STRUCT IMPLEMENTING IT.
 * @param node_a A pointer to the first node
 * @param node_b A pointer to the second node
 * @return > 0 if a > b, < 0 if a < b, 0 if a == b
 */
typedef int (*avl_compare_t)(struct _avl_node_t *node_a, struct _avl_node_t *node_b);

/**
 * @brief An update callback function that can be called every time "avl_update" is called on a node. Example: Keeping track of max size in subtree. OPTIONAL. 
 * @param node The node to be updated.
 */
typedef void (*avl_update_t)(struct _avl_node_t *node);

typedef struct _avl_node_t {
    //Pointers to the other nodes in the tree.
    struct _avl_node_t *parent;
    struct _avl_node_t *left;
    struct _avl_node_t *right;

    //Used for quick balance check calc by keeping the subtree max depth.
    uint64_t subtree_max_depth;

    //Update CAN be null.
    avl_update_t update;
} avl_node_t;

typedef struct {
    avl_node_t *root;
    size_t size; //Count of elements in the tree.
} avl_tree_t;

//All public data structure functions.
void avl_insert(avl_tree_t *tree, avl_node_t *node_to_insert, avl_compare_t comp);
void avl_remove(avl_tree_t *tree, avl_node_t *node_to_remove);

//Search
avl_node_t *avl_get_node(avl_tree_t *tree, avl_node_t *key, avl_compare_t comp);

//Traversal
avl_node_t *avl_first(avl_tree_t *tree);
avl_node_t *avl_last(avl_tree_t *tree);
avl_node_t *avl_next(avl_node_t *node);
avl_node_t *avl_prev(avl_node_t *node);
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/memory/vma.h"
#include "../include/memory/vmm.h"
#include "../include/memory/pmm.h"
#include "../include/common/math.h"
#include "../include/common/stdtypes.h"
#include "../include/common/printf.h"

#define MINIMUM_AVAILABLE_NODES 5

vm_ds_node *kernel_vma_heap_tree_root = NULL;
vm_ds_node *kernel_vma_mmio_tree_root = NULL;
vm_ds_node *free_list = NULL;
uint64_t free_node_count = 0;
uint8_t in_replenish_cycle = 0;

//The non data structure implementations
void replenish_slab_from_tree();
vm_ds_node *alloc_vm_ds_node();
void free_vm_ds_node(vm_ds_node *node);
void *vma_allocate_memory_from_tree(vm_ds_node **root, uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
void vma_free_memory_from_tree(vm_ds_node **root, uint64_t start_addr);

//All VM Data Structure functions
void vm_ds_init_node(vm_ds_node *node, uint64_t start, uint64_t size, vm_node_type type);
vm_ds_node *vm_ds_insert(vm_ds_node *root, vm_ds_node *node_to_insert);
void vm_ds_update(vm_ds_node *node);
vm_ds_node *vm_ds_check_and_balance(vm_ds_node *node);
int vm_ds_get_balance_factor(vm_ds_node *node);
vm_ds_node *vm_ds_rotate_right(vm_ds_node *node);
vm_ds_node *vm_ds_rotate_left(vm_ds_node *node);
vm_ds_node *vm_ds_bubble_update_and_balance(vm_ds_node *node);
vm_ds_node *vm_ds_remove(vm_ds_node *root, vm_ds_node *node_to_remove);
vm_ds_node *vm_ds_get_node(vm_ds_node *root, uint64_t addr);
vm_ds_node *vm_ds_find_worst_fit(vm_ds_node *root, uint64_t size);
vm_ds_node *vm_ds_get_predecessor(vm_ds_node *node);
vm_ds_node *vm_ds_get_successor(vm_ds_node *node);
static void vma_print_tree(vm_ds_node *root);

uint8_t vma_demand_paging(uint64_t fault_addr, uint8_t is_user) {
    //We start by checking if it is user or supervisor that triggered this to search the peoper tree.
    vm_ds_node *node_for_address;
    if (is_user) node_for_address = NULL; //TODO: Implement when userland exists.
    else node_for_address = vm_ds_get_node(kernel_vma_heap_tree_root, fault_addr); //Only kernel heap is tracked by VMA and has demand paging.

    if (!node_for_address) return 1; //Return error that the address is in fact invalid.
    if (node_for_address->type == VMA_FREE) return 1; //Also return error if the address is still marked as free.
    //TODO: Handle file-backed memory.
    //We handle the conventional memory here.
    //We then need to allocate a frame for this.
    uint64_t physical_frame = (uint64_t)pmm_allocate_frames(1, 4096);
    //And then map it to the page this address is part of.
    vmm_map(fault_addr, physical_frame, 1, node_for_address->flags);
    return 0;
}

void vma_init() {
    //We need to initialize the kernel VMA tree. For this, we need a page to hold our starting nodes for the tree.
    //We request a page from the PMM then map it to the base of the heap.
    uint64_t phys_addr = (uint64_t)pmm_allocate_frames(1, 4096);
    vmm_map(KERNEL_HEAP_START, phys_addr, 1, PT_GLOBAL | PT_WRITEABLE | PT_NX);

    //We want to get as many nodes that can fit in one page
    uint32_t node_per_page = 4096 / sizeof(vm_ds_node);
    vm_ds_node *start_of_page = (vm_ds_node*)KERNEL_HEAP_START;

    //We then set them up to be put in our free list. We go until the before-last one. The last will point to the other list.
    for (uint32_t i = 0; i < node_per_page - 1; i++) {
        start_of_page[i].start_addr = (uint64_t)(start_of_page + i + 1);
    }

    //We make sure to add the nodes to our node counter!
    free_node_count += node_per_page;

    //We then set our free list.
    free_list = start_of_page;

    //Now, we can init our kernel heap tree. We start with a node representing the node page we just requested.
    kernel_vma_heap_tree_root = alloc_vm_ds_node();
    vm_ds_init_node(kernel_vma_heap_tree_root, KERNEL_HEAP_START, 4096, VMA_REGULAR);
    kernel_vma_heap_tree_root->flags = PT_GLOBAL | PT_WRITEABLE | PT_NX;

    //Then, we create the next node representing the rest of kernel heap space which we add to the kernel tree.
    vm_ds_node *kheap_remaining = alloc_vm_ds_node();
    vm_ds_init_node(kheap_remaining, KERNEL_HEAP_START + 4096, KERNEL_HEAP_SIZE - 4096, VMA_FREE);
    vm_ds_insert(kernel_vma_heap_tree_root, kheap_remaining);

    //After the heap init, we can simply do the MMIO one.
    kernel_vma_mmio_tree_root = alloc_vm_ds_node();
    vm_ds_init_node(kernel_vma_mmio_tree_root, KERNEL_MMIO_START, KERNEL_MMIO_SIZE, VMA_FREE);
}

void vma_free_memory_from_ktree(uint64_t start_addr) {
    if (start_addr >= KERNEL_HEAP_START && start_addr < KERNEL_HEAP_LIMIT) vma_free_memory_from_tree(&kernel_vma_heap_tree_root, start_addr);
    else if (start_addr >= KERNEL_MMIO_START && start_addr < KERNEL_MMIO_LIMIT) vma_free_memory_from_tree(&kernel_vma_mmio_tree_root, start_addr);
}

void vma_free_memory_from_utree(uint64_t start_addr) {
    return; //TODO: Implement when user space exists
}

void *vma_allocate_memory_from_ktree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {
    //We allocate to the heap tree unless it is an MMIO request.
    if (allocation_type == VMA_HARDWARE_MMIO) {
        return vma_allocate_memory_from_tree(&kernel_vma_mmio_tree_root, size, allocation_type, flags, allocation_backing);
    }
    return vma_allocate_memory_from_tree(&kernel_vma_heap_tree_root, size, allocation_type, flags, allocation_backing);
}

void *vma_allocate_memory_from_utree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {
    return NULL; //TODO: Implement when userspace exists
}

void *vma_allocate_memory_from_tree(vm_ds_node **root, uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {
    //DEBUG
    //kprintf("\nTree before allocation:\n");
    //vma_print_tree(*root);
    
    //allocation_backing will be copied over, node will NOT point to the specific allocation_backing passed in. NULL can be passed when allocating normal memory.
    if (allocation_type == VMA_FREE) return NULL;
    //We need to locate the worst fit for this request.
    vm_ds_node *node_for_request = vm_ds_find_worst_fit(*root, size);

    //We will allocate from the start to either the whole block or up to a part.
    if (size == node_for_request->size) {
        //Since the node is EXACTLY the right size, we can simply update the flags and type.
        node_for_request->type = allocation_type;
        node_for_request->flags = flags;
    }
    else {
        //We need to get a node for the remaining hole after the allocation.
        vm_ds_node *node_for_leftover = alloc_vm_ds_node();
        uint64_t leftover_start = node_for_request->start_addr + size;
        vm_ds_init_node(node_for_leftover, leftover_start, node_for_request->size - size, VMA_FREE);

        //Start by prepping the requested node with the data from the request.
        node_for_request->type = allocation_type;
        node_for_request->flags = flags;
        node_for_request->size = size;

        //Then, simply insert into the tree.
        *root = vm_ds_insert(*root, node_for_leftover);
    }

    //Now, we need to do the backing for the virtual memory.
    //Normal memory does not need to be saved in node, since page tables are enough. Lazy paging will use those.

    if (allocation_type == VMA_HARDWARE_MMIO) {
        //MMIO can have its address saved in the node, since it is guaranteed to be continuous.
        node_for_request->backing.mmio.physical_start = allocation_backing->mmio.physical_start;

        //Also, no lazy load for this. We will ask the VMM to map asap.
        vmm_map(node_for_request->start_addr, allocation_backing->mmio.physical_start, size / 4096, flags);
    }
    else if (allocation_type == VMA_FILE_BACKED) {
        //File backed virtual memory will be using lazy loading.
        node_for_request->backing.file.file_ptr = allocation_backing->file.file_ptr;
        node_for_request->backing.file.offset = allocation_backing->file.file_ptr;
    }
    //DEBUG
    //kprintf("\nTree after allocation:\n");
    //vma_print_tree(*root);

    return (void *)node_for_request->start_addr;
}

void vma_free_memory_from_tree(vm_ds_node **root, uint64_t start_addr) {
    //DEBUG
    //kprintf("\nTree before free:\n");
    //vma_print_tree(*root);

    //We first need to find this node from the tree. It needs to be the start_address of the requested block.
    vm_ds_node *node_to_free = vm_ds_get_node(*root, start_addr);

    //We then need to free the underlying physical memory and invalidate the vmm mapping.
    if (node_to_free->type == VMA_HARDWARE_MMIO) {
        //Since it is not allocated from the PMM, we can simply unmap it via the VMM.
        vmm_unmap(node_to_free->start_addr, node_to_free->size / 4096);
    }
    else if (node_to_free->type == VMA_FILE_BACKED || node_to_free->type == VMA_REGULAR) {
        //Both of these will have possibly non-contiguous frames in RAM. We need to free them in PMM before doing VMM unmap.
        for (uint64_t current_v_addr = node_to_free->start_addr; current_v_addr < node_to_free->start_addr + node_to_free->size; current_v_addr += 0x1000) {
            uint64_t phys_addr = vmm_get_physical_from_virtual(current_v_addr);
            pmm_free_frames((void *)phys_addr, 1);
        }
        
        //Then unmap!
        vmm_unmap(node_to_free->start_addr, node_to_free->size / 4096);
    }

    //Now, we need to check to see if the successor and/or predecessor are also "free" to coalesce them.
    vm_ds_node *successor = vm_ds_get_successor(node_to_free);
    vm_ds_node *predecessor = vm_ds_get_predecessor(node_to_free);

    //Now, we have three cases:
    //Case 1: Both pre/suc are free
    if ((successor && successor->type == VMA_FREE) && (predecessor && predecessor->type == VMA_FREE)) {
        //We calculate the new block size, which is all three blocks together.
        uint64_t new_block_size = successor->size + predecessor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        predecessor->size = new_block_size;

        //We then delete both other nodes!
        *root = vm_ds_remove(*root, successor);
        *root = vm_ds_remove(*root, node_to_free);

        //Lastly, readd them to the free node pool.
        successor->start_addr = (uint64_t)node_to_free;
        node_to_free->start_addr = (uint64_t)free_list;
        free_list = successor;
        free_node_count += 2;
    }
    //Case 2: pre is free
    else if (predecessor && predecessor->type == VMA_FREE) {
        //We calculate the new block size, which is both blocks together.
        uint64_t new_block_size = predecessor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        predecessor->size = new_block_size;

        //We then delete the other node!
        *root = vm_ds_remove(*root, node_to_free);

        //Lastly, readd them to the free node pool.
        node_to_free->start_addr = (uint64_t)free_list;
        free_list = node_to_free;
        free_node_count++;
    }
    //Case 3: suc is free
    else if (successor && successor->type == VMA_FREE) {
        //We calculate the new block size, which is both blocks together.
        uint64_t new_block_size = successor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        node_to_free->size = new_block_size;
        node_to_free->type = VMA_FREE;

        //We then delete the other node!
        *root = vm_ds_remove(*root, successor);

        //Lastly, readd them to the free node pool.
        successor->start_addr = (uint64_t)free_list;
        free_list = successor;
        free_node_count++;
    }
    //Case 4: suc/pre are either null and/or not free
    else {
        //We just mark the node as free.
        node_to_free->type = VMA_FREE;
    }
    //DEBUG
    //kprintf("\nTree after free:\n");
    //vma_print_tree(*root);
}

void replenish_slab_from_tree() {
    //We want to get as many nodes that can fit in one page
    uint32_t node_per_page = 4096 / sizeof(vm_ds_node);
    vm_ds_node *start_of_page = (vm_ds_node*)vma_allocate_memory_from_tree(&kernel_vma_heap_tree_root, 4096, VMA_REGULAR, PT_GLOBAL | PT_WRITEABLE | PT_NX, NULL);

    //We then set them up to be put in our free list. We go until the before-last one. The last will point to the other list, handled outside this loop.
    for (uint32_t i = 0; i < node_per_page - 1; i++) {
        start_of_page[i].start_addr = (uint64_t)(start_of_page + i + 1);
    }

    //We make sure to add the nodes to our node counter!
    free_node_count += node_per_page;

    //We then connect our node list to the current one.
    start_of_page[node_per_page - 1].start_addr = (uint64_t)free_list;
    free_list = start_of_page;
}

vm_ds_node *alloc_vm_ds_node() {
    //We start by getting the node we will return from our freelist.
    vm_ds_node *node_to_allocate = free_list;
    free_list = (vm_ds_node*)free_list->start_addr;
    free_node_count--;

    //We clear the node. Using builtin for now. May change later if I want a libc style memset.
    __builtin_memset(node_to_allocate, 0, sizeof(vm_ds_node));

    //We need to keep enough nodes available, so if they drop under the threshold, we allocate a page to the VMA for more nodes.
    if (free_node_count < MINIMUM_AVAILABLE_NODES && !in_replenish_cycle) {
        in_replenish_cycle = 1;
        replenish_slab_from_tree();
        in_replenish_cycle = 0;
    }
    return node_to_allocate;
}

void free_vm_ds_node(vm_ds_node *node) {
    node->start_addr = (uint64_t)free_list;
    free_list = node;
    free_node_count++;
}

//All VM Data Structure functions are following

void vm_ds_init_node(vm_ds_node *node, uint64_t start, uint64_t size, vm_node_type type) {
    node->start_addr = start;
    node->size = size;
    node->type = type;
    
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;

    node->subtree_max_depth = 0;
    node->subtree_max_free_slot = size;
}

vm_ds_node *vm_ds_insert(vm_ds_node *root, vm_ds_node *node_to_insert) {
    //In case root changes due to balance, etc.
    vm_ds_node *new_root = root;
    //The parent pointer on the to-be-inserted node. Will be the real parent since the real one is the last to update this.
    node_to_insert->parent = root;
    //We figure out if we need to go as left or right of this, and add it as a child if we can slot it as leaf.
    uint64_t start_to_insert = node_to_insert->start_addr;
    if (start_to_insert < root->start_addr){
        if (root->left) root->left = vm_ds_insert(root->left, node_to_insert);
        else {
            root->left = node_to_insert;
            node_to_insert->parent = root;
        }
    } 
    else if (start_to_insert >= root->start_addr) {
        if (root->right) root->right = vm_ds_insert(root->right, node_to_insert);
        else {
            root->right = node_to_insert;
            node_to_insert->parent = root;
        }
    }

    //If we get here, we are going back up the call stack. We need to clean up starting at the parent.
    vm_ds_update(root);
    new_root = vm_ds_check_and_balance(root);
    return new_root;
}

void vm_ds_update(vm_ds_node *node) {
    if (!node) return;
    //Start by calculating the max free slot size for the subtree. It will be either the data from the chilren's subtrees or the node itself may be the biggest.
    uint64_t max_size_left_subtree = (node->left) ? node->left->subtree_max_free_slot : 0;
    uint64_t max_size_right_subtree = (node->right) ? node->right->subtree_max_free_slot : 0;
    uint64_t node_size_to_be_considered = (node->type == VMA_FREE) ? node->size : 0;
    node->subtree_max_free_slot = MAX(MAX(max_size_left_subtree, max_size_right_subtree), node_size_to_be_considered);

    //Max depth calculation. Simply the max of either child, and add 1.
    int max_depth_left_subtree = (node->left) ? node->left->subtree_max_depth : -1;
    int max_depth_right_subtree = (node->right) ? node->right->subtree_max_depth : -1;
    node->subtree_max_depth = MAX(max_depth_left_subtree, max_depth_right_subtree) + 1;
}

//Check and balance AVL tree.
vm_ds_node *vm_ds_check_and_balance(vm_ds_node *node) {
    vm_ds_node *subtree_root = node;
    int balance = vm_ds_get_balance_factor(node);

    //Check for imbalances.
    //If ok, simply return.
    if (balance < 2 && balance > -2) return subtree_root;
    //If not, start by checking if it is a left side imbalance.
    if (balance > 0) {
        //Check if it is a left-left imbalance. If the balance of that node is positive, it means that the left child's subtree is imbalanced on the left side.
        if (vm_ds_get_balance_factor(node->left) > 0) {
            //If yes, we need a single right rotation.
            subtree_root = vm_ds_rotate_right(node);
        }
        else {
            //If not, it is left-right. we then need a left rotation, followed by right rotation.
            node->left = vm_ds_rotate_left(node->left);
            subtree_root = vm_ds_rotate_right(node);
        }
    }
    //If not left side, then we know its a right imbalance.
    else {
        //We check for right-right or right-left imbalance.
        if (vm_ds_get_balance_factor(node->right) < 0) {
            //Right right can be solved with a left rotation on the current node.
            subtree_root = vm_ds_rotate_left(node);
        }
        else {
            //Right left is fixed by right rotation on the right child, followed by left rotation on parent.
            node->right = vm_ds_rotate_right(node->right);
            subtree_root = vm_ds_rotate_left(node);
        }
    }

    return subtree_root;
}

int vm_ds_get_balance_factor(vm_ds_node *node) {
    //Start by getting the max depth of the right or left subtree, including parent node.
    uint64_t max_depth_left_subtree = (node->left) ? node->left->subtree_max_depth + 1 : 0;
    uint64_t max_depth_right_subtree = (node->right) ? node->right->subtree_max_depth + 1 : 0;
    //Calculate the balance, AKA left - right subtree max size.
    return max_depth_left_subtree - max_depth_right_subtree;
}

vm_ds_node *vm_ds_rotate_right(vm_ds_node *node) {
    //A right rotation will make the left child the new subtree root.
    vm_ds_node *new_subtree_root = node->left;
    //Get the current subtree's parent. Null if this is the whole tree.
    vm_ds_node *subtree_parent = node->parent;

    //The current root will take the left child's right child as its left child.
    node->left = node->left->right;
    if (node->left) node->left->parent = node;

    //The new subtree root which used to be the node's left child will take the node as it's right child.
    new_subtree_root->right = node;
    
    //Update the parents for both the node and new root.
    new_subtree_root->parent = subtree_parent;
    node->parent = new_subtree_root;

    //Make sure to update the new root's kids and itself.
    vm_ds_update(new_subtree_root->left);
    vm_ds_update(new_subtree_root->right);
    vm_ds_update(new_subtree_root);
    return new_subtree_root;
}

vm_ds_node *vm_ds_rotate_left(vm_ds_node *node) {
    //A left rotation will make the right child the new subtree root.
    vm_ds_node *new_subtree_root = node->right;
    //Get the current subtree's parent. Null if root of complete tree.
    vm_ds_node *subtree_parent = node->parent;

    //The current node will take the right child's left child as it's right child.
    node->right = node->right->left;
    if (node->right) node->right->parent = node;

    //The new subtree root which used to be the node's right child will take the node as it's left child.
    new_subtree_root->left = node;

    //Update the parents for both the node and new root.
    new_subtree_root->parent = subtree_parent;
    node->parent = new_subtree_root;

    //Make sure to update the new root's kids and itself.
    vm_ds_update(new_subtree_root->left);
    vm_ds_update(new_subtree_root->right);
    vm_ds_update(new_subtree_root);
    return new_subtree_root;
}

vm_ds_node *vm_ds_bubble_update_and_balance(vm_ds_node *node) {
    if (!node) return NULL;
    while (1) {
        //Start by updating the data for this node.
        vm_ds_update(node);

        //Then, we need to keep track of the old node we were on to keep it's pointer.
        vm_ds_node *old_node = node;

        //Node may get overwritten since it would be the new root of the subtree.
        node = vm_ds_check_and_balance(node);

        //When we reach the root, 
        if (!node->parent) return node;

        //We need to manually update the parent node's children to reflect the new subtree root. Make sure to check against the old root to update the proper child.
        if (old_node == node->parent->left) node->parent->left = node;
        else node->parent->right = node;

        node = node->parent;
    }
}

vm_ds_node *vm_ds_get_node(vm_ds_node *root, uint64_t addr) {
    if (!root) return NULL;
    if (root->start_addr <= addr && root->start_addr + root->size > addr) return root;
    if (root->start_addr > addr) return vm_ds_get_node(root->left, addr);
    return vm_ds_get_node(root->right, addr);
}

//Function is not responsible for freeing the node! Do not lose the pointer!
vm_ds_node *vm_ds_remove(vm_ds_node *root, vm_ds_node *node_to_remove) {
    //We keep a reference to the deepest node we need to bauble updates at the end.
    vm_ds_node *node_to_update = NULL;

    //We will start by checking what type of deletion we will be dealing with.
    //Type 1: No children
    if (!node_to_remove->left && !node_to_remove->right) {
        //Easiest one. Simply make the parent point to nothing. 
        if (node_to_remove->parent->left == node_to_remove) node_to_remove->parent->left = NULL;
        else node_to_remove->parent->right = NULL;
        node_to_update = node_to_remove->parent;
    }
    //Type 3: 2 Children
    else if (node_to_remove->left && node_to_remove->right) {
        //Complicated one. We need to replace this node with its inorder successor. (Smallest node in right subtree)
        //So, lets start by finding this successor:
        vm_ds_node *victim_node = vm_ds_get_successor(node_to_remove);

        //Now, we have the victim. This node itself will have to be "deleted" from the tree. It will then be manually added back here.
        //This wont be a infinite recursive loop since the successor will not have a left child.
        vm_ds_remove(root, victim_node);

        //Now, we simply make the victim take its place by making the to-be-deleted node's parent point to it and it point to the kids.
        victim_node->left = node_to_remove->left;
        victim_node->left->parent = victim_node;

        victim_node->right = node_to_remove->right;
        victim_node->right->parent = victim_node;

        victim_node->parent = node_to_remove->parent;

        node_to_update = victim_node;
    }
    //Type 2: 1 Child
    else {
        //Simply make the parent point to this node's child.
        vm_ds_node *child;
        child = (node_to_remove->left) ? node_to_remove->left : node_to_remove->right;
        child->parent = node_to_remove->parent;
        if (node_to_remove->parent->left == node_to_remove) node_to_remove->parent->left = child;
        else node_to_remove->parent->right = child;
        node_to_update = node_to_remove->parent;
    }

    //Now, its cleanup time!
    return vm_ds_bubble_update_and_balance(node_to_update);
}

vm_ds_node *vm_ds_find_worst_fit(vm_ds_node *root, uint64_t size) {
    //If we get to a state where the max free slot is smaller than size, then no hole can fit the request
    if (root->subtree_max_free_slot < size) return NULL;

    //Now, we want the max_free_slot so we find it :)
    if (root->size == root->subtree_max_free_slot && root->type == VMA_FREE) return root;
    if (root->left && root->left->subtree_max_free_slot == root->subtree_max_free_slot) return vm_ds_find_worst_fit(root->left, size);
    if (root->right && root->right->subtree_max_free_slot == root->subtree_max_free_slot) return vm_ds_find_worst_fit(root->right, size);

    //A return in case the tree is broken or something.
    return NULL;
}

vm_ds_node *vm_ds_get_predecessor(vm_ds_node *node) {
    vm_ds_node *predecessor = NULL;

    if (node->left != NULL) {
        //Case A: If there is a left child, go left once, then all the way right
        predecessor = node->left;
        while (predecessor->right != NULL) {
            predecessor = predecessor->right;
        }
    } else {
        //Case B: No left child. Walk up the parent chain until you find that the subtree we came from was parent's right child.
        vm_ds_node *curr = node;
        vm_ds_node *p = node->parent;
        while (p != NULL && curr == p->left) {
            curr = p;
            p = p->parent;
        }
        predecessor = p;
    }

    return predecessor;
}


vm_ds_node *vm_ds_get_successor(vm_ds_node *node) {
    vm_ds_node *successor = NULL;

    if (node->right != NULL) {
        //Case A: If there is a right child, go right once, then all the way left
        successor = node->right;
        while (successor->left != NULL) {
            successor = successor->left;
        }
    } else {
        // Case B: No right child. Walk up the parent chain until you find that the subtree we came from was parent's left child.
        vm_ds_node *curr = node;
        vm_ds_node *p = node->parent;
        while (p != NULL && curr == p->right) {
            curr = p;
            p = p->parent;
        }
        successor = p;
    }

    return successor;
}

static void vma_print_tree(vm_ds_node *root) {
    //Start with left child.
    if (root->left) vma_print_tree(root->left);

    //Now, we do the root itself.
    kprintf("\nNext Node!\n");
    kprintf("Node address: %lx\n", (uint64_t)root);
    kprintf("Node parent: %lx\n", (uint64_t)root->parent);
    kprintf("Node left: %lx\n", (uint64_t)root->left);
    kprintf("Node right: %lx\n", (uint64_t)root->right);
    kprintf("Start_address: %lx\n", root->start_addr);
    kprintf("Size: %lx\n", root->size);
    kprintf("Type: %u\n", root->type);
    kprintf("Flags: %lx\n", root->flags);
    kprintf("Max free slot subtree: %lx\n", root->subtree_max_free_slot);
    kprintf("Max depth subtree: %lx\n", root->subtree_max_depth);

    //Then right child.
    if (root->right) vma_print_tree(root->right);
}
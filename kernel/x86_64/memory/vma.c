/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_VMM
#define CURRENT_LOG_NAME "VMA"

#include <memory/vma.h>
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <common/math.h>
#include <common/stdtypes.h>
#include <common/printf.h>
#include <common/logging.h>

#define MINIMUM_AVAILABLE_NODES 5

avl_tree_t kernel_vma_heap_tree;
avl_tree_t kernel_vma_mmio_tree;
vm_ds_node *free_list = NULL;
uint64_t free_node_count = 0;
uint8_t in_replenish_cycle = 0;

//The non data structure implementations
static void replenish_slab_from_tree();
static vm_ds_node *alloc_vm_ds_node();
static void free_vm_ds_node(vm_ds_node *node);
static void *vma_allocate_memory_from_tree(avl_tree_t *tree, uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing);
static void vma_free_memory_from_tree(avl_tree_t *tree, uint64_t start_addr);
static void init_vm_ds_node(vm_ds_node *node, uint64_t start, uint64_t size, vm_node_type type, avl_update_t update_callback);

//All VM AVL Data Structure functions
static void vm_avl_update(avl_node_t *node);
static int vm_avl_comp_addr(avl_node_t *node_a, avl_node_t *node_b);
static int vm_avl_comp_addr_space(avl_node_t *node_a, avl_node_t *node_b);
static int vm_avl_comp_max_free_slot(avl_node_t *node_a, avl_node_t *node_b);

uint8_t vma_demand_paging(uint64_t fault_addr) {
    vm_ds_node temp_key;
    temp_key.start_addr = fault_addr;
    //We start by checking if it is user or supervisor that triggered this to search the peoper tree.
    vm_ds_node *node_for_address;
    if (fault_addr <= 0x00007FFFFFFFFFFF) node_for_address = NULL; //TODO: Implement when userland exists.
    else node_for_address = (vm_ds_node*)avl_get_node(&kernel_vma_heap_tree, (avl_node_t*)(&temp_key), vm_avl_comp_addr_space); //Only kernel heap is tracked by VMA and has demand paging.

    if (!node_for_address) {
        LOG_E("Address %lx is not a valid virtual address in requested tree.\n", fault_addr);
        return 1; //Return error that the address is in fact invalid.
    } 
    if (node_for_address->type != VMA_REGULAR) {
        LOG_E("Address %lx is not an address type which supports demand paging. (not VMA_REGULAR)\n", fault_addr);
        return 1; //Also return error if the address is not of demand paging type.
    } 
    //TODO: Handle file-backed memory.
    //We handle the conventional memory here.
    //We then need to allocate a frame for this.
    uint64_t physical_frame = (uint64_t)pmm_allocate_frames(1, 4096);
    //And then map it to the page this address is part of.
    vmm_map(fault_addr, physical_frame, 1, node_for_address->flags);
    LOG_D("Demand paging mapped address %lx successfully.\n", fault_addr);
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
    
    vm_ds_node *kernel_vma_heap_tree_root = alloc_vm_ds_node();
    init_vm_ds_node(kernel_vma_heap_tree_root, KERNEL_HEAP_START, 4096, VMA_REGULAR, vm_avl_update);
    kernel_vma_heap_tree_root->flags = PT_GLOBAL | PT_WRITEABLE | PT_NX;
    kernel_vma_heap_tree.root = (avl_node_t*)kernel_vma_heap_tree_root;

    //Then, we create the next node representing the rest of kernel heap space which we add to the kernel tree.
    vm_ds_node *kheap_remaining = alloc_vm_ds_node();
    init_vm_ds_node(kheap_remaining, KERNEL_HEAP_START + 4096, KERNEL_HEAP_SIZE - 4096, VMA_FREE, vm_avl_update);
    avl_insert(&kernel_vma_heap_tree, (avl_node_t*)kheap_remaining, vm_avl_comp_addr);

    //After the heap init, we can simply do the MMIO one.
    vm_ds_node *kernel_vma_mmio_tree_root = alloc_vm_ds_node();
    init_vm_ds_node(kernel_vma_mmio_tree_root, KERNEL_MMIO_START, KERNEL_MMIO_SIZE, VMA_FREE, vm_avl_update);
    kernel_vma_mmio_tree.root = (avl_node_t*)kernel_vma_mmio_tree_root;
}

void vma_free_memory_from_ktree(uint64_t start_addr) {
    if (start_addr >= KERNEL_HEAP_START && start_addr < KERNEL_HEAP_LIMIT) vma_free_memory_from_tree(&kernel_vma_heap_tree, start_addr);
    else if (start_addr >= KERNEL_MMIO_START && start_addr < KERNEL_MMIO_LIMIT) vma_free_memory_from_tree(&kernel_vma_mmio_tree, start_addr);
}

void vma_free_memory_from_utree(uint64_t start_addr) {
    return; //TODO: Implement when user space exists
}

void *vma_allocate_memory_from_ktree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {

    //We allocate to the heap tree unless it is an MMIO request. When unmanaged, the kernel trees are either MMIO tree or HEAP.
    if (allocation_type == VMA_UNMANAGED_MAPPING && allocation_backing->unmanaged.tree == VMA_TREE_KMMIO) {
        return vma_allocate_memory_from_tree(&kernel_vma_mmio_tree, size, allocation_type, flags, allocation_backing);
    }
    if (allocation_type == VMA_HARDWARE_MMIO) {
        return vma_allocate_memory_from_tree(&kernel_vma_mmio_tree, size, allocation_type, flags, allocation_backing);
    }
    return vma_allocate_memory_from_tree(&kernel_vma_heap_tree, size, allocation_type, flags, allocation_backing);
}

void *vma_allocate_memory_from_utree(uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {
    return NULL; //TODO: Implement when userspace exists
}

static void *vma_allocate_memory_from_tree(avl_tree_t *tree, uint64_t size, vm_node_type allocation_type, uint32_t flags, vma_backing *allocation_backing) {
    //DEBUG
    //kprintf("\nTree before allocation:\n");
    //vma_print_tree(*root);
    
    //allocation_backing will be copied over, node will NOT point to the specific allocation_backing passed in. NULL can be passed when allocating normal memory.
    if (allocation_type == VMA_FREE) return NULL;
    LOG_D("Received allocation request of size %lu, type %u, flags %u.\n", size, allocation_type, flags);
    //We need to locate the worst fit for this request.
    vm_ds_node *node_for_request = (vm_ds_node*)avl_get_node(tree, NULL, vm_avl_comp_max_free_slot);

    //NULL check
    if (!node_for_request) {
        LOG_E("Could not fulfill request. Find worst fit returned NULL meaning out of contiguous virtual memory in this tree at size %lu.\n", size);
        return NULL;
    }

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
        init_vm_ds_node(node_for_leftover, leftover_start, node_for_request->size - size, VMA_FREE, vm_avl_update);

        //Start by prepping the requested node with the data from the request.
        node_for_request->type = allocation_type;
        node_for_request->flags = flags;
        node_for_request->size = size;

        //Then, simply insert into the tree.
        avl_insert(tree, (avl_node_t*)node_for_leftover, vm_avl_comp_addr);
    }

    LOG_D("Found slot starting at address %lx.\n", node_for_request->start_addr);

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
    else if (allocation_type == VMA_UNMANAGED_MAPPING) {
        //In unmanaged, the VMA will do nothing else and simply hold the address.
        node_for_request->backing.unmanaged.tree = allocation_backing->unmanaged.tree;
    }
    //DEBUG
    //kprintf("\nTree after allocation:\n");
    //vma_print_tree(*root);

    return (void *)node_for_request->start_addr;
}

static void vma_free_memory_from_tree(avl_tree_t *tree, uint64_t start_addr) {
    //DEBUG
    //kprintf("\nTree before free:\n");
    //vma_print_tree(*root);

    LOG_D("Received free request of address %lx.\n", start_addr);

    //We first need to find this node from the tree. It needs to be the start_address of the requested block.
    vm_ds_node temp_key;
    temp_key.start_addr = start_addr;
    vm_ds_node *node_to_free = (vm_ds_node*)avl_get_node(tree, (avl_node_t*)(&temp_key), vm_avl_comp_addr);

    if (!node_to_free) {
        LOG_E("No node with address %lx could be found in tree.\n", start_addr);
    }

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
    //UNMANAGED will not require processing by the VMA. It should only reclaim the virtual memory space.

    //Now, we need to check to see if the successor and/or predecessor are also "free" to coalesce them.
    vm_ds_node *successor = (vm_ds_node*)avl_next((avl_node_t*)node_to_free);
    vm_ds_node *predecessor = (vm_ds_node*)avl_prev((avl_node_t*)node_to_free);

    //Now, we have three cases:
    //Case 1: Both pre/suc are free
    if ((successor && successor->type == VMA_FREE) && (predecessor && predecessor->type == VMA_FREE)) {
        LOG_D("Executing case 1 coalescing.\n");
        //We calculate the new block size, which is all three blocks together.
        uint64_t new_block_size = successor->size + predecessor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        predecessor->size = new_block_size;

        //We then delete both other nodes!
        avl_remove(tree, (avl_node_t*)successor);
        avl_remove(tree, (avl_node_t*)node_to_free);

        //Lastly, readd them to the free node pool.
        successor->start_addr = (uint64_t)node_to_free;
        node_to_free->start_addr = (uint64_t)free_list;
        free_list = successor;
        free_node_count += 2;
    }
    //Case 2: pre is free
    else if (predecessor && predecessor->type == VMA_FREE) {
        LOG_D("Executing case 2 coalescing.\n");
        //We calculate the new block size, which is both blocks together.
        uint64_t new_block_size = predecessor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        predecessor->size = new_block_size;

        //We then delete the other node!
        avl_remove(tree, (avl_node_t*)node_to_free);

        //Lastly, readd them to the free node pool.
        node_to_free->start_addr = (uint64_t)free_list;
        free_list = node_to_free;
        free_node_count++;
    }
    //Case 3: suc is free
    else if (successor && successor->type == VMA_FREE) {
        LOG_D("Executing case 3 coalescing.\n");
        //We calculate the new block size, which is both blocks together.
        uint64_t new_block_size = successor->size + node_to_free->size;

        //We then make the block with the lowest address consume the others, since start_addr is what matters. 
        node_to_free->size = new_block_size;
        node_to_free->type = VMA_FREE;

        //We then delete the other node!
        avl_remove(tree, (avl_node_t*)successor);

        //Lastly, readd them to the free node pool.
        successor->start_addr = (uint64_t)free_list;
        free_list = successor;
        free_node_count++;
    }
    //Case 4: suc/pre are either null and/or not free
    else {
        LOG_D("Executing case 4 coalescing.\n");
        //We just mark the node as free.
        node_to_free->type = VMA_FREE;
    }
    //DEBUG
    //kprintf("\nTree after free:\n");
    //vma_print_tree(*root);
}

static void replenish_slab_from_tree() {
    LOG_D("VMA free node list is low, replenishing slab from kheap.\n");
    //We want to get as many nodes that can fit in one page
    uint32_t node_per_page = 4096 / sizeof(vm_ds_node);
    vm_ds_node *start_of_page = (vm_ds_node*)vma_allocate_memory_from_tree(&kernel_vma_heap_tree, 4096, VMA_REGULAR, PT_GLOBAL | PT_WRITEABLE | PT_NX, NULL);

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

static vm_ds_node *alloc_vm_ds_node() {
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

static void free_vm_ds_node(vm_ds_node *node) {
    node->start_addr = (uint64_t)free_list;
    free_list = node;
    free_node_count++;
}

static void init_vm_ds_node(vm_ds_node *node, uint64_t start, uint64_t size, vm_node_type type, avl_update_t update_callback) {
    node->start_addr = start;
    node->size = size;
    node->type = type;
    node->subtree_max_free_slot = size;
    avl_init((avl_node_t*)node, update_callback);
}

//All VM Data Structure functions are following

static void vm_avl_update(avl_node_t *node) {
    //Cast the node(s) to its subclass, which is the specific vm_ds_node.
    vm_ds_node *complete_node = (vm_ds_node*)node;
    vm_ds_node *left = (vm_ds_node*)(node->left);
    vm_ds_node *right = (vm_ds_node*)(node->right);

    uint64_t max_size_left_subtree = (left) ? left->subtree_max_free_slot : 0;
    uint64_t max_size_right_subtree = (right) ? right->subtree_max_free_slot : 0;
    uint64_t node_size_to_be_considered = (complete_node->type == VMA_FREE) ? complete_node->size : 0;
    complete_node->subtree_max_free_slot = MAX(MAX(max_size_left_subtree, max_size_right_subtree), node_size_to_be_considered);
}

//Used during insertions.
static int vm_avl_comp_addr(avl_node_t *node_a, avl_node_t *node_b) {
    //Cast to full nodes.
    vm_ds_node *node_a_complete = (vm_ds_node*)(node_a);
    vm_ds_node *node_b_complete = (vm_ds_node*)(node_b);

    return node_a_complete->start_addr - node_b_complete->start_addr;
}

//Used to find a node from an address which may be within the bounds of sstart_addr + size.
static int vm_avl_comp_addr_space(avl_node_t *node_a, avl_node_t *node_b) {
    //Cast to full nodes.
    vm_ds_node *node_a_complete = (vm_ds_node*)(node_a);
    vm_ds_node *node_b_complete = (vm_ds_node*)(node_b);

    if (node_a_complete->start_addr <= node_b_complete->start_addr && node_a_complete->start_addr + node_a_complete->size > node_b_complete->start_addr) return 0;
    if (node_a_complete->start_addr > node_b_complete->start_addr) return 1;
    return -1;
}

//Used to find the worst fit.
static int vm_avl_comp_max_free_slot(avl_node_t *node_a, avl_node_t *node_b) {
    //Cast to full nodes.
    vm_ds_node *node_a_complete = (vm_ds_node*)(node_a);
    vm_ds_node *left = (vm_ds_node*)(node_a->left);
    vm_ds_node *right = (vm_ds_node*)(node_a->right);

    //Now, we want the max_free_slot so we find it :)
    if (node_a_complete->size == node_a_complete->subtree_max_free_slot && node_a_complete->type == VMA_FREE) return 0;
    if (node_a->left && left->subtree_max_free_slot == node_a_complete->subtree_max_free_slot) return 1;
    return -1;
}
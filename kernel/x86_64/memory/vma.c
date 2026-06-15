#include "../include/memory/vma.h"
#include "../include/memory/vmm.h"
#include "../include/memory/pmm.h"
#include "../include/common/math.h"
#include "../include/common/stdtypes.h"

void vm_ds_init_node(vm_ds_node *node, uint64_t start, uint64_t size);
vm_ds_node *vm_ds_insert(vm_ds_node *current_node, vm_ds_node *node_to_insert);
void vm_ds_update(vm_ds_node *node);
vm_ds_node *vm_ds_check_and_balance(vm_ds_node *node);
int vm_ds_get_balance_factor(vm_ds_node *node);
vm_ds_node *vm_ds_rotate_right(vm_ds_node *node);
vm_ds_node *vm_ds_rotate_left(vm_ds_node *node);
void vm_ds_bubble_update(vm_ds_node *node);

void vm_ds_init_node(vm_ds_node *node, uint64_t start, uint64_t size) {
    node->start_addr = start;
    node->size = size;
    
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;

    node->subtree_max_depth = 0;
    node->subtree_max_free_slot = size;
}

vm_ds_node *vm_ds_insert(vm_ds_node *current_node, vm_ds_node *node_to_insert) {
    //In case root changes due to balance, etc.
    vm_ds_node *new_root = current_node;
    //The parent pointer on the to-be-inserted node. Will be the real parent since the real one is the last to update this.
    node_to_insert->parent = current_node;
    //We figure out if we need to go as left or right of this, and add it as a child if we can slot it as leaf.
    uint64_t start_to_insert = node_to_insert->start_addr;
    if (start_to_insert < current_node->start_addr){
        if (current_node->left) current_node->left = vm_ds_insert(current_node->left, node_to_insert);
        else {
            current_node->left = node_to_insert;
            node_to_insert->parent = current_node;
        }
    } 
    else if (start_to_insert >= current_node->start_addr) {
        if (current_node->right) current_node->right = vm_ds_insert(current_node->right, node_to_insert);
        else {
            current_node->right = node_to_insert;
            node_to_insert->parent = current_node;
        }
    }

    //If we get here, we are going back up the call stack. We need to clean up starting at the parent.
    vm_ds_update(current_node);
    new_root = vm_ds_check_and_balance(current_node);
    return new_root;
}

void vm_ds_update(vm_ds_node *node) {
    //Start by calculating the max free slot size for the subtree. It will be either the data from the chilren's subtrees or the node itself may be the biggest.
    uint64_t max_size_left_subtree = (node->left) ? node->left->subtree_max_free_slot : 0;
    uint64_t max_size_right_subtree = (node->right) ? node->right->subtree_max_free_slot : 0;
    node->subtree_max_free_slot = (MAX(max_size_left_subtree, max_size_right_subtree), node->size);

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

void vm_ds_bubble_update(vm_ds_node *node) {
    while (node) {
        vm_ds_update(node);
        node = node->parent;
    }
}
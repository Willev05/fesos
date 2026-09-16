/* File: avl.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <common/ds/avl.h>
#include <common/math.h>

//All private Data Structure functions
void avl_update(avl_node_t *node);
avl_node_t *avl_check_and_balance(avl_node_t *node);
int avl_get_balance_factor(avl_node_t *node);
avl_node_t *avl_rotate_right(avl_node_t *node);
avl_node_t *avl_rotate_left(avl_node_t *node);
avl_node_t *avl_bubble_update_and_balance(avl_node_t *node);
avl_node_t *avl_get_predecessor(avl_node_t *node);
avl_node_t *avl_get_successor(avl_node_t *node);
avl_node_t *avl_insert_rec(avl_node_t *root, avl_node_t *node_to_insert, avl_compare_t comp);
avl_node_t *avl_get_node_rec(avl_node_t *root, avl_node_t *key, avl_compare_t comp);

//Implementation of public functions

/**
 * @brief Inserts `node_to_insert` into `tree` using `comp` to find its appropriate location.
 * @param tree A pointer to the tree in which to insert.
 * @param node_to_insert The node to insert into the tree.
 * @param comp The callback function provided to tell insert where to insert your node. 
 */
void avl_insert(avl_tree_t *tree, avl_node_t *node_to_insert, avl_compare_t comp) {
    if (!tree->root) {
        tree->root = node_to_insert;
        return;
    } 
    //This is basically a wrapper so the caller does not have to remember to save a potential new root.
    avl_node_t *new_root = avl_insert_rec(tree->root, node_to_insert, comp);
    tree->root = new_root;
}

/**
 * @brief Rmoves `node_to_remove` from `tree`.
 * @param tree A pointer to the tree from which to remove.
 * @param node_to_insert The node to remove from the tree.
 */
void avl_remove(avl_tree_t *tree, avl_node_t *node_to_remove) {
    if (!tree->root) return;
    //We keep a reference to the deepest node we need to bauble updates at the end.
    avl_node_t *node_to_update = NULL;

    //We will start by checking what type of deletion we will be dealing with.
    //Type 1: No children
    if (!node_to_remove->left && !node_to_remove->right) {
        //Easiest one. Simply make the parent point to nothing.
        //Check frist if the node has parents. If not, then you are removing the only node "root". 
        if (!node_to_remove->parent) tree->root = NULL;
        else if (node_to_remove->parent->left == node_to_remove) node_to_remove->parent->left = NULL;
        else node_to_remove->parent->right = NULL;
        node_to_update = node_to_remove->parent;
    }
    //Type 3: 2 Children
    else if (node_to_remove->left && node_to_remove->right) {
        //Complicated one. We need to replace this node with its inorder successor. (Smallest node in right subtree)
        //So, lets start by finding this successor:
        avl_node_t *victim_node = avl_get_successor(node_to_remove);

        //Now, we have the victim. This node itself will have to be "deleted" from the tree. It will then be manually added back here.
        //This wont be a infinite recursive loop since the successor will not have a left child.
        avl_remove(tree, victim_node);

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
        avl_node_t *child;
        child = (node_to_remove->left) ? node_to_remove->left : node_to_remove->right;
        child->parent = node_to_remove->parent;
        //If the node to remove has no parents, then the only child becomes root.
        if (!node_to_remove->parent) tree->root = child;
        if (node_to_remove->parent->left == node_to_remove) node_to_remove->parent->left = child;
        else node_to_remove->parent->right = child;
        node_to_update = node_to_remove->parent;
    }

    //Now, its cleanup time!
    avl_node_t *new_root = avl_bubble_update_and_balance(node_to_update);
    tree->root = new_root;
}

/**
 * @brief Returns the node from `tree` matching the `key` node.
 * @param tree The tree to search. 
 * @param key The key node. Must match the desired params used by provided `comp` function. Ex: If comp checks start_addr, then key must have desired start_addr.
 * @param comp The comparator function used to see if the node is found, is in left subtree, or is in right subtree.
 * @return The node, if found, NULL if not.
 */
avl_node_t *avl_get_node(avl_tree_t *tree, avl_node_t *key, avl_compare_t comp) {
    //Once again, wrapper.
    if (!tree->root) return NULL;
    return avl_get_node_rec(tree->root, key, comp);
}

/**
 * @brief Returns the first (leftmost) node. Normally, the smallest node.
 * @param tree The tree to seatch.
 * @return The first node.
 */
avl_node_t *avl_first(avl_tree_t *tree) {
    if (!tree->root) return NULL;
    avl_node_t *current_node = tree->root;
    while (current_node->left) current_node = current_node->left;
    return current_node;
}

/**
 * @brief Returns the last (rightmost) node. Normally, the largest node.
 * @param tree The tree to search.
 * @return The last node.
 */
avl_node_t *avl_last(avl_tree_t *tree) {
    if (!tree->root) return NULL;
    avl_node_t *current_node = tree->root;
    while (current_node->right) current_node = current_node->right;
    return current_node;
}

/**
 * @brief Returns the next node (in-order successor).
 * @param node The node for which to find the next.
 * @return The next node.
 */
avl_node_t *avl_next(avl_node_t *node) {
    if (!node) return NULL;
    return avl_get_successor(node);
}

/**
 * @brief Returns the previous node (in-order predecessor).
 * @param node The node for which to find the previous.
 * @return The previous node.
 */
avl_node_t *avl_prev(avl_node_t *node) {
    if (!node) return NULL;
    return avl_get_predecessor(node);
}

//Implementation of private functions
void avl_update(avl_node_t *node) {
    if (!node) return;

    //Max depth calculation. Simply the max of either child, and add 1.
    int max_depth_left_subtree = (node->left) ? node->left->subtree_max_depth : -1;
    int max_depth_right_subtree = (node->right) ? node->right->subtree_max_depth : -1;
    node->subtree_max_depth = MAX(max_depth_left_subtree, max_depth_right_subtree) + 1;

    //If the node has a custom on update callback, call it.
    if (node->update) node->update(node);
}

avl_node_t *avl_check_and_balance(avl_node_t *node) {
    avl_node_t *subtree_root = node;
    int balance = avl_get_balance_factor(node);

    //Check for imbalances.
    //If ok, simply return.
    if (balance < 2 && balance > -2) return subtree_root;
    //If not, start by checking if it is a left side imbalance.
    if (balance > 0) {
        //Check if it is a left-left imbalance. If the balance of that node is positive, it means that the left child's subtree is imbalanced on the left side.
        if (avl_get_balance_factor(node->left) > 0) {
            //If yes, we need a single right rotation.
            subtree_root = avl_rotate_right(node);
        }
        else {
            //If not, it is left-right. we then need a left rotation, followed by right rotation.
            node->left = avl_rotate_left(node->left);
            subtree_root = avl_rotate_right(node);
        }
    }
    //If not left side, then we know its a right imbalance.
    else {
        //We check for right-right or right-left imbalance.
        if (avl_get_balance_factor(node->right) < 0) {
            //Right right can be solved with a left rotation on the current node.
            subtree_root = avl_rotate_left(node);
        }
        else {
            //Right left is fixed by right rotation on the right child, followed by left rotation on parent.
            node->right = avl_rotate_right(node->right);
            subtree_root = avl_rotate_left(node);
        }
    }

    return subtree_root;
}

int avl_get_balance_factor(avl_node_t *node) {
    //Start by getting the max depth of the right or left subtree, including parent node.
    uint64_t max_depth_left_subtree = (node->left) ? node->left->subtree_max_depth + 1 : 0;
    uint64_t max_depth_right_subtree = (node->right) ? node->right->subtree_max_depth + 1 : 0;
    //Calculate the balance, AKA left - right subtree max size.
    return max_depth_left_subtree - max_depth_right_subtree;
}

avl_node_t *avl_rotate_right(avl_node_t *node) {
    //A right rotation will make the left child the new subtree root.
    avl_node_t *new_subtree_root = node->left;
    //Get the current subtree's parent. Null if this is the whole tree.
    avl_node_t *subtree_parent = node->parent;

    //The current root will take the left child's right child as its left child.
    node->left = node->left->right;
    if (node->left) node->left->parent = node;

    //The new subtree root which used to be the node's left child will take the node as it's right child.
    new_subtree_root->right = node;
    
    //Update the parents for both the node and new root.
    new_subtree_root->parent = subtree_parent;
    node->parent = new_subtree_root;

    //Make sure to update the new root's kids and itself.
    avl_update(new_subtree_root->left);
    avl_update(new_subtree_root->right);
    avl_update(new_subtree_root);
    return new_subtree_root;
}

avl_node_t *avl_rotate_left(avl_node_t *node) {
    //A left rotation will make the right child the new subtree root.
    avl_node_t *new_subtree_root = node->right;
    //Get the current subtree's parent. Null if root of complete tree.
    avl_node_t *subtree_parent = node->parent;

    //The current node will take the right child's left child as it's right child.
    node->right = node->right->left;
    if (node->right) node->right->parent = node;

    //The new subtree root which used to be the node's right child will take the node as it's left child.
    new_subtree_root->left = node;

    //Update the parents for both the node and new root.
    new_subtree_root->parent = subtree_parent;
    node->parent = new_subtree_root;

    //Make sure to update the new root's kids and itself.
    avl_update(new_subtree_root->left);
    avl_update(new_subtree_root->right);
    avl_update(new_subtree_root);
    return new_subtree_root;
}

avl_node_t *avl_bubble_update_and_balance(avl_node_t *node) {
    if (!node) return NULL;
    while (1) {
        //Start by updating the data for this node.
        avl_update(node);

        //Then, we need to keep track of the old node we were on to keep it's pointer.
        avl_node_t *old_node = node;

        //Node may get overwritten since it would be the new root of the subtree.
        node = avl_check_and_balance(node);

        //When we reach the root, 
        if (!node->parent) return node;

        //We need to manually update the parent node's children to reflect the new subtree root. Make sure to check against the old root to update the proper child.
        if (old_node == node->parent->left) node->parent->left = node;
        else node->parent->right = node;

        node = node->parent;
    }
}

avl_node_t *avl_get_predecessor(avl_node_t *node){
    avl_node_t *predecessor = NULL;

    if (node->left != NULL) {
        //Case A: If there is a left child, go left once, then all the way right
        predecessor = node->left;
        while (predecessor->right != NULL) {
            predecessor = predecessor->right;
        }
    } else {
        //Case B: No left child. Walk up the parent chain until you find that the subtree we came from was parent's right child.
        avl_node_t *curr = node;
        avl_node_t *p = node->parent;
        while (p != NULL && curr == p->left) {
            curr = p;
            p = p->parent;
        }
        predecessor = p;
    }

    return predecessor;
}

avl_node_t *avl_get_successor(avl_node_t *node) {
    avl_node_t *successor = NULL;

    if (node->right != NULL) {
        //Case A: If there is a right child, go right once, then all the way left
        successor = node->right;
        while (successor->left != NULL) {
            successor = successor->left;
        }
    } else {
        // Case B: No right child. Walk up the parent chain until you find that the subtree we came from was parent's left child.
        avl_node_t *curr = node;
        avl_node_t *p = node->parent;
        while (p != NULL && curr == p->right) {
            curr = p;
            p = p->parent;
        }
        successor = p;
    }

    return successor;
}

avl_node_t *avl_insert_rec(avl_node_t *root, avl_node_t *node_to_insert, avl_compare_t comp) {
    //In case root changes due to balance, etc.
    avl_node_t *new_root = root;
    //The parent pointer on the to-be-inserted node. Will be the real parent since the real one is the last to update this.
    node_to_insert->parent = root;
    //We figure out if we need to go as left or right of this, and add it as a child if we can slot it as leaf.
    int comp_res = comp(root, node_to_insert);
    //If the node to insert is smaller than the current node.
    if (comp_res > 0){
        if (root->left) root->left = avl_insert_rec(root->left, node_to_insert, comp);
        else {
            root->left = node_to_insert;
            node_to_insert->parent = root;
        }
    } 
    else {
        if (root->right) root->right = avl_insert_rec(root->right, node_to_insert, comp);
        else {
            root->right = node_to_insert;
            node_to_insert->parent = root;
        }
    }

    //If we get here, we are going back up the call stack. We need to clean up starting at the parent.
    avl_update(root);
    new_root = avl_check_and_balance(root);
    return new_root;
}

avl_node_t *avl_get_node_rec(avl_node_t *root, avl_node_t *key, avl_compare_t comp) {
    if (!root) return NULL;
    int comp_ans = comp(root, key);
    if (comp_ans == 0) return root;
    if (comp_ans > 0) return avl_get_node_rec(root->left, key, comp);
    return avl_get_node_rec(root->right, key, comp);
}


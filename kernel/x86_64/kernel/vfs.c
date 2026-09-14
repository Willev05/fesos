/* File: vfs.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_DRIVERS
#define CURRENT_LOG_NAME "VFS"

#include <kernel/vfs.h>
#include <common/logging.h>
#include <kernel/errno.h>
#include <memory/kmalloc.h>

/**
 * @brief Read from a file in the vfs.
 * @param node The vfs node representing the file to read from.
 * @param offset The offset in bytes to start reading from.
 * @param size The size in bytes to read.
 * @param buffer A pointer to the destination buffer.
 * @return 0 on success, negative error codes when applicable.
 */
int vfs_read(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer) {
    if (!node) {
        LOG_E("Node passed to read is null.\n");
        return -EINVAL;
    }
    if (!size) {
        LOG_W("Size passed to read is 0. Returning without calling driver.\n");
        return 0;
    }

    LOG_D("Received read-request for node name %s at inode %u.\n", node->name, node->inode);

    //Check to see if this node can even support the read operation.
    if (node->type == VFS_NODE_DIRECTORY) {
        LOG_E("Node is directory. Cannot execute read.\n");
        return -EISDIR;
    }

    //Check if the file supports read operation.
    if (!node->operations || !node->operations->read) {
        LOG_E("File does not support read.\n");
        return -EPERM;
    }

    LOG_D("Passing read-request to driver.\n");
    return node->operations->read(node, offset, size, buffer);
}

/**
 * @brief Write to a file in the vfs.
 * @param node The vfs node representing the file to write to.
 * @param offset The offset in bytes to start writing to.
 * @param size The size in bytes to write.
 * @param buffer A pointer to the source buffer.
 * @return 0 on success, negative error codes when applicable.
 */
int vfs_write(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer) {
    if (!node) {
        LOG_E("Node passed to write is null.\n");
        return -EINVAL;
    }
    if (!size) {
        LOG_W("Size passed to write is 0. Returning without calling driver.\n");
        return 0;
    }

    LOG_D("Received write-request for node name %s at inode %u.\n", node->name, node->inode);

    //Check to see if this node can even support the write operation.
    if (node->type == VFS_NODE_DIRECTORY) {
        LOG_E("Node is directory. Cannot execute write.\n");
        return -EISDIR;
    }

    //Check if the file supports write operation.
    if (!node->operations || !node->operations->write) {
        LOG_E("File does not support write.\n");
        return -EPERM;
    }

    LOG_D("Passing write-request to driver.\n");
    return node->operations->write(node, offset, size, buffer);
}

/**
 * @brief Close a file. This will decrease the internal ref counter. If at 0, will truly close the file and release resources.
 * @param node The node representing the file.
 * @return 0 on success, negative error code when applicable.
 */
int vfs_close(vfs_node_t *node) {
    if (!node) {
        LOG_E("Node passed to close is null.\n");
        return -EINVAL;
    }

    LOG_D("Received close-request for node name %s at inode %u.\n", node->name, node->inode);

    node->ref_count--;
    //If 0, then we need to tell the driver so it can clean up. We then release the node.
    if (!node->ref_count) {
        int driver_status = 0;
        //Check if the file supports write operation.
        if (node->operations && node->operations->close) {
            driver_status = node->operations->close(node);
        }

        //Free the vfs node.
        kfree(node);

        LOG_D("Finished closing node.\n");
        return driver_status;
    }
}

/**
 * @brief Open a file. This will increase the internal ref counter. Will always notify the driver.
 * @param node The node representing the file.
 * @return 0 on success, negative error code when applicable.
 */
int vfs_open(vfs_node_t *node) {
    if (!node) {
        LOG_E("Node passed to open is null.\n");
        return -EINVAL;
    }

    LOG_D("Received open-request for node name %s at inode %u.\n", node->name, node->inode);

    node->ref_count++;
    //We need to call the driver open function, if applicable.
    int driver_status = 0;
    //Check if the file supports open operation.
    if (node->operations && node->operations->open) {
        driver_status = node->operations->open(node);
        //If driver status did not return a 0, need to rollback the ref_count.
        if (driver_status) {
            node->ref_count--;
            LOG_E("Could not open file. Returning error code.\n");
            return driver_status;
        }
    }

    LOG_D("Open operation complete.\n");
    return driver_status;
}
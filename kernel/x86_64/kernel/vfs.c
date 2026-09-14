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

/**
 * @brief Finds a child node called `name` on the node.
 * @param node The node on which to perform the search.
 * @param name The name of the child node to seek.
 * @return The node, if found.
 */
vfs_node_t *vfs_finddir(vfs_node_t *node, char *name) {
    if (!node) {
        LOG_E("Node passed to finddir is null.\n");
        return NULL;
    }
    LOG_D("Received finddir-request for node name %s at inode %u.\n", node->name, node->inode);
    if (!name) {
        LOG_E("Name string passed to finddir is null.\n");
        return NULL;
    }
    if (node->type != VFS_NODE_DIRECTORY) {
        LOG_E("Node is not directory. Can not execute finddir.\n");
        return NULL;
    }
    if (!node->operations->finddir) {
        LOG_E("Directory does not support finddir.\n");
        return NULL;
    }

    //Simply relay the command to the driver backing this node.
    LOG_D("Finddir request checks passed. Passing to driver.\n");
    return node->operations->finddir(node, name);
}

/**
 * @brief Read the directory entries in node `node`. Will take the Nth-index (`index`) and fill the struct at `dirent`.
 * @param node The directory node to read from.
 * @param index The index into the directory entries to read.
 * @param dirent A pointer to the dirent struct to fill.
 * @return 1 on success, 0 on end-of-array (or invalid bound).
 */
int vfs_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *dirent) {
    if (!node) {
        LOG_E("Node passed to readdir is null.\n");
        return 0;
    }
    LOG_D("Received readdir-request for node name %s at inode %u. Passed index: %u.\n", node->name, node->inode, index);
    if (!dirent) {
        LOG_E("dirent passed to readdir is null.\n");
        return 0;
    }
    if (node->type != VFS_NODE_DIRECTORY) {
        LOG_E("Node is not directory. Can not execute readdir.\n");
        return 0;
    }
    if (!node->operations->readdir) {
        LOG_E("Directory does not support readdir.\n");
        return 0;
    }

    //Simply relay the command to the driver backing this node.
    LOG_D("Readdir request checks passed. Passing to driver.\n");
    return node->operations->readdir(node, index, dirent);
}

/**
 * @brief Creates a new child node of `name` and `type` onto `parent` node.
 * @param parent The parent node on which to add a new child node.
 * @param name The name of the child node.
 * @param type The type of the child node.
 * @return 0 on success, negative error code when applicable.
 */
int vfs_create(vfs_node_t *parent, char *name, vfs_node_type_t type) {
    if (!parent) {
        LOG_E("Parent node passed to create is null.\n");
        return -EINVAL;
    }
    LOG_D("Received create-request for parent node name %s at inode %u. Passed child name: %s and type: %u.\n", parent->name, parent->inode, name, type);
    if (!name) {
        LOG_E("Name passed to create is null.\n");
        return -EINVAL;
    }
    if (parent->type != VFS_NODE_DIRECTORY) {
        LOG_E("Node is not directory. Can not execute create.\n");
        return 0;
    }
    if (!parent->operations->create) {
        LOG_E("Directory does not support create.\n");
        return 0;
    }

    //Pass to the driver.
    LOG_D("Create request checks passed. Passing to driver.\n");
    return parent->operations->create(parent, name, type);
}

/**
 * @brief Sends an I/O control call to the driver backing the file. Used for deeper interaction with devices/drivers.
 * @param node The node for which to send the request.
 * @param command_id The command id for the driver to execute.
 * @param args A pointer to a struct containing extra info the request. This varies depending on backing driver/device/command.
 */
int vfs_ioctl(vfs_node_t *node, uint32_t command_id, void *args) {
    if (!node) {
        LOG_E("Node passed to ioctl is null.\n");
        return -EINVAL;
    }
    LOG_D("Received ioctl-request for parent node name %s at inode %u.\n", node->name, node->inode);
    if (node->type == VFS_NODE_DIRECTORY || !node->operations->ioctl) {
        LOG_E("ioctl not supported on node.\n");
        return -ENOTTY;
    }

    return node->operations->ioctl(node, command_id, args);
}
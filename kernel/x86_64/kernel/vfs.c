/* File: vfs.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_DRIVERS
#define CURRENT_LOG_NAME "VFS"

#include <kernel/vfs.h>
#include <common/logging.h>
#include <kernel/errno.h>
#include <memory/kmalloc.h>
#include <common/stdstr.h>

static vfs_node_t *vfs_root_node;

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

    //Update refcounts
    node->ref_count--;
    node->mountpoint->ref_count--;
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

    //Update refcounts
    node->ref_count++;
    node->mountpoint->ref_count++;
    //We need to call the driver open function, if applicable.
    int driver_status = 0;
    //Check if the file supports open operation.
    if (node->operations && node->operations->open) {
        driver_status = node->operations->open(node);
        //If driver status did not return a 0, need to rollback the ref_count.
        if (driver_status) {
            node->ref_count--;
            node->mountpoint->ref_count--;
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
 * @param len The length of the `name` string.
 * @return The node, if found.
 */
vfs_node_t *vfs_finddir(vfs_node_t *node, char *name, size_t len) {
    if (!node) {
        LOG_E("Node passed to finddir is null.\n");
        return NULL;
    }
    LOG_D("Received finddir-request for node name %s at inode %u.\n", node->name, node->inode);
    if (!name) {
        LOG_E("Name string passed to finddir is null.\n");
        return NULL;
    }
    if (!len) {
        LOG_E("Length of string passed to finddir is 0. Not a valid name.\n");
        return NULL;
    }

    //If this is a mountpoint, we need to change the node to point to the root of this mounted system.
    if (node->type == VFS_NODE_MOUNTPOINT && node->mountpoint) {
        node = node->mountpoint;
    }

    if (node->type != VFS_NODE_DIRECTORY) {
        LOG_E("Node is not directory. Can not execute finddir.\n");
        return NULL;
    }
    if (!node->operations || !node->operations->finddir) {
        LOG_E("Directory does not support finddir.\n");
        return NULL;
    }

    //Simply relay the command to the driver backing this node.
    LOG_D("Finddir request checks passed. Passing to driver.\n");
    return node->operations->finddir(node, name, len);
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
    //If this is a mountpoint, we need to change the node to point to the root of this mounted system.
    if (node->type == VFS_NODE_MOUNTPOINT && node->mountpoint) {
        node = node->mountpoint;
    }
    if (node->type != VFS_NODE_DIRECTORY) {
        LOG_E("Node is not directory. Can not execute readdir.\n");
        return 0;
    }
    if (!node->operations || !node->operations->readdir) {
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
    if (!parent->operations || !parent->operations->create) {
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
    if (node->type == VFS_NODE_DIRECTORY || !node->operations || !node->operations->ioctl) {
        LOG_E("ioctl not supported on node.\n");
        return -ENOTTY;
    }

    return node->operations->ioctl(node, command_id, args);
}

/**
 * @brief Lookup a file/directory in the vfs using `path`.
 * @param path The path to the wanted file/directory.
 * @return The node, if applicable.
 */
vfs_node_t *vfs_lookup(char *path) {
    if (!path) {
        LOG_E("Path passed to lookup is null.\n");
        return -EINVAL;
    }
    LOG_D("Received lookup for path %s.\n", path);

    size_t path_len = str_len(path);
    if (path_len == 0) {
        LOG_E("Path passed to lookup is empty.\n");
        return -EINVAL;
    }

    vfs_node_t *current_node;
    uint32_t left_ptr, right_ptr; //Left inclusive, right exclusive.

    //We need to check if the path is absolute.
    if (path[0] == '/') {
        //If so, skip over the slash and start searching at the root.
        left_ptr = 1;
        right_ptr = 1;
        current_node = vfs_root_node;
    }
    else {
        LOG_E("Relative path not implemented!!!");
        return -EPERM;
    }

    //This is the main loop, it will start with getting the next path string, see if it exists and perform search if so.
    while (1) {
        //Get the file/directory name.
        while (path[right_ptr] != '/' && path[right_ptr] != 0) right_ptr++;
        //If we exited the, check to see if right and left are one apart, if so, then we disregard and keep going (if applicable).
        if (left_ptr - right_ptr > 1) {
            //We see at least 1 character that is not /. We get the name and pass it to finddir.
            current_node = vfs_finddir(current_node, &path[left_ptr], right_ptr - left_ptr - 1);
        }

        //Now, we run some checks to see if we should stop.
        if (!current_node) {
            //This happens if the directory contains no child. finddir will return null.
            LOG_E("File/directory not found: %s", path);
            return -ENOENT;
        }

        //If node is ok, look at the remaining path. If the right ptr is null char, this means end of path.
        if (!path[right_ptr]) {
            break;
        }
    }

    //If we reach here, we got the file/directory.
    LOG_D("Found file.\n");
    return current_node;
}

int vfs_mount(char *mount_path, vfs_node_t *fs_root) {
    if (!fs_root) {
        LOG_D("Filesystem root passed to mount is null.\n");
        return -EINVAL;
    }
    if (!mount_path) {
        LOG_D("Mount path passed to mount is null.\n");
        return -EINVAL;
    }
    LOG_D("Received mount request for name: %s inode: %u at path %s.\n", fs_root->name, fs_root->inode, mount_path);

    //Get the directory to act as a mountpoint.
    vfs_node_t *mount_stub = vfs_lookup(mount_path);
    if (!mount_path) {
        LOG_E("Mount path %s does not exist.\n", mount_path);
        return -ENOENT;
    }
    //Check if a directory, to become a valid mountpoint.
    if (mount_stub->type != VFS_NODE_DIRECTORY) {
        LOG_E("Mount path %s is not a directory.\n", mount_path);
        return -ENOTDIR;
    }

    mount_stub->type = VFS_NODE_MOUNTPOINT;
    mount_stub->mount_ptr = fs_root;

    //Update the ref_count for tracking.
    mount_stub->ref_count++;
    mount_stub->mountpoint->ref_count++;
    fs_root->ref_count++;

    return 0;
}
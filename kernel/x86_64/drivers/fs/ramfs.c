/* File: ramfs.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <drivers/fs/ramfs.h>
#include <memory/kmalloc.h>
#include <common/stdstr.h>

#define DIRCUTOFF 0x10000
#define FILECOUNT 1
#define DIRCOUNT 2
#define DIRENTCOUNT 2

#define FILETYPE 0
#define DIRTYPE 1

static int ramfs_read(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer);
static int ramfs_close(vfs_node_t *node);
static vfs_node_data_t ramfs_finddir(vfs_node_t *node, char *name, size_t len);
static int ramfs_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *dirent);

typedef struct _ramfs_dirent_t {
    char *name; 
    uint8_t type;
    uint16_t index;
} ramfs_dirent_t;

typedef struct _ramfs_file_t {
    void *data_ptr;
    size_t size;
} ramfs_file_t;

typedef struct _ramfs_dir_t {
    ramfs_dirent_t **dircont;
    size_t contcount;
} ramfs_dir_t;

ramfs_file_t files[FILECOUNT];
ramfs_dir_t dirs[DIRCOUNT];
ramfs_dirent_t dirent[DIRENTCOUNT];

vfs_ops_t file_ops = {
    .close = ramfs_close,
    .create = NULL,
    .finddir = NULL,
    .ioctl = NULL,
    .open = NULL,
    .read = ramfs_read,
    .readdir = NULL,
    .write = NULL
};
vfs_ops_t dir_ops = {
    .close = ramfs_close,
    .create = NULL,
    .finddir = ramfs_finddir,
    .ioctl = NULL,
    .open = NULL,
    .read = NULL,
    .readdir = ramfs_readdir,
    .write = NULL
};

void ramfs_init() {
    dirent[0].type = DIRTYPE;
    dirent[0].index = 1;
    dirent[0].name = "subdirtest";

    dirent[1].type = FILETYPE;
    dirent[1].index = 0;
    dirent[1].name = "test";

    files[0].data_ptr = kmalloc(8);
    files[0].size = 8;
    *(uint64_t*)files[0].data_ptr = 0xABCDEF;

    dirs[0].dircont = kmalloc(8 * 1);
    dirs[0].contcount = 1;
    dirs[0].dircont[0] = &dirent[0];

    dirs[1].dircont = kmalloc(8 * 1);
    dirs[1].contcount = 1;
    dirs[1].dircont[0] = &dirent[1];
}

vfs_node_t *ramfs_get_fs() {
    vfs_node_t *new_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    new_node->inode = DIRCUTOFF;
    str_ncpy("ramfs_root", new_node->name, 10);
    new_node->operations = &dir_ops;
    new_node->mount_ptr = NULL;
    new_node->mountpoint = new_node;
    new_node->type = VFS_NODE_DIRECTORY;
    return new_node;
}

static int ramfs_read(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer) {
    ramfs_file_t *file = &files[node->inode];
    if (offset + size > file->size) return -1;

    uint8_t *next_bs = (uint8_t*)file->data_ptr;
    next_bs += offset;
    uint8_t *next_bd = buffer;

    for (size_t counter = 0; counter < size; counter++) {
        *next_bd = *next_bs;
        next_bd++;
        next_bs++;
    }

    return 0;
}

static int ramfs_close(vfs_node_t *node) {
    //Only cleanup if we are releasing the root/unmounting.
    if (node->inode != DIRCUTOFF) return 0;

    for (size_t filecount = 0; filecount < FILECOUNT; filecount++) {
        kfree(files[filecount].data_ptr);
    }
    for (size_t dircount = 0; dircount < DIRCOUNT; dircount++) {
        kfree(dirs[dircount].dircont);
    }
    return 0;
}

static vfs_node_data_t ramfs_finddir(vfs_node_t *node, char *name, size_t len) {
    uint32_t dir_index = node->inode - DIRCUTOFF;
    ramfs_dirent_t **dircont = dirs[dir_index].dircont;
    for(uint32_t dir_ent = 0; dir_ent < dirs[dir_index].contcount; dir_ent++) {
        if (!str_ncmp(dircont[dir_ent]->name, name, str_len(dircont[dir_ent]->name), len)) {
            //Found it. 
            vfs_node_data_t new_node_data;
            if (dircont[dir_ent]->type == DIRTYPE) {
                new_node_data.inode = dircont[dir_ent]->index + DIRCUTOFF;
                new_node_data.operations = &dir_ops;
                new_node_data.type = VFS_NODE_DIRECTORY;
            }
            else {
                new_node_data.inode = dircont[dir_ent]->index;
                new_node_data.operations = &file_ops;
                new_node_data.type = VFS_NODE_FILE;
            }

            return new_node_data;
        }
    }
    vfs_node_data_t invalid_node_data = {.operations = NULL};
    return invalid_node_data;
}

static int ramfs_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *dirent) {
    uint32_t dir_index = node->inode - DIRCUTOFF;
    ramfs_dirent_t **dircont = dirs[dir_index].dircont;
    ramfs_dirent_t *ram_dirent = dircont[index];
    str_ncpy(ram_dirent->name, dirent->name, str_len(ram_dirent->name));
    if (ram_dirent->type == DIRTYPE) {
        dirent->inode = ram_dirent->index + DIRCUTOFF;
        dirent->size = 0;
        dirent->type = VFS_NODE_DIRECTORY;
    }
    else {
        dirent->inode = ram_dirent->index;
        ramfs_file_t *file = &files[ram_dirent->index];
        dirent->size = file->size;
        dirent->type = VFS_NODE_FILE;
    }
    return 0;
}
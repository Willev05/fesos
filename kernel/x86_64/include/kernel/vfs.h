/* File: vfs.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <common/stdtypes.h>

typedef enum {
    VFS_NODE_FILE,
    VFS_NODE_DIRECTORY,
    VFS_NODE_MOUNTPOINT
} vfs_node_type_t;

struct _vfs_node_t;
typedef int (*vfs_read_t)(struct _vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer);
typedef int (*vfs_write_t)(struct _vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer);
typedef int (*vfs_close_t)(struct _vfs_node_t *node);
typedef int (*vfs_open_t)(struct _vfs_node_t *node);
typedef struct _vfs_node_t *(*vfs_finddir_t)(struct _vfs_node_t *node, char *name);
typedef int (*vfs_readdir_t)(struct _vfs_node_t *node);
typedef int (*vfs_create_t)(struct _vfs_node_t *parent, char *name, vfs_node_type_t type);
typedef int (*vfs_ioctl_t)(struct _vfs_node_t *node, void *args);


typedef struct {
    vfs_read_t read;
    vfs_write_t write;
    vfs_close_t close;
    vfs_open_t open;
    vfs_finddir_t finddir;
    vfs_readdir_t readdir;
    vfs_create_t create;
    vfs_ioctl_t ioctl;
} vfs_ops_t;

typedef struct _vfs_node_t {
    char name[128];
    uint32_t inode;
    vfs_node_type_t type;
    uint32_t ref_count;

    vfs_ops_t *operations;
} vfs_node_t;

//IO redirect functions

int vfs_read(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer);
int vfs_write(vfs_node_t *node, size_t offset, size_t size, uint8_t *buffer);
int vfs_close(vfs_node_t *node);
int vfs_open(vfs_node_t *node);
vfs_node_t *vfs_finddir(vfs_node_t *node, char *name);
int vfs_readdir(vfs_node_t *node);
int vfs_create(vfs_node_t *parent, char *name, vfs_node_type_t type);
int vfs_ioctl(vfs_node_t *node, void *args);

//VFS functions

vfs_node_t *vfs_lookup(char *path);
vfs_node_t *vfs_mount(char *mount_path, vfs_node_t *fs_root);
int vfs_unmount(char *path);
void vfs_init();
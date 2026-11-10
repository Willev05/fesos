/* File: fs.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include <kernel/vfs.h>
#include <drivers/block/partman.h>

#define MAX_FS_DRIVERS 64

typedef struct {
    vfs_ops_t *operations;
    fs_probe_t *probe;
    char name[16]
} fs_driver_t;

typedef struct {
    fs_driver_t *drivers[MAX_FS_DRIVERS];
    uint64_t count;
} fs_driver_registry_t;

extern fs_driver_registry_t fs_driver_registry;

int drivers_pci_register(fs_driver_t driver);
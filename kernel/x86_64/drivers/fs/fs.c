/* File: fs.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <drivers/fs/fs.h>
#include <kernel/errno.h>

fs_driver_registry_t fs_driver_registry;

/**
 * @brief Registers a driver to the file system driver registry. Allows driver to be used by vfs and partman.
 * @param driver The actual pci driver struct. 
 * returns 0 on success, EPERM if registry full. 
 */
int fs_driver_register(fs_driver_t *driver) {
    if (fs_driver_registry.count >= MAX_FS_DRIVERS) {
        LOG_E("Unable to register FS driver %s. Maximum FS drivers registered reached.\n", driver->name);
        return -EPERM;
    }

    //Check as well to see if name is unique, since name is used to assign vfs nodes to drivers and such.
    for (int i = 0; i < fs_driver_registry.count; i++) {
        if (driver->name == fs_driver_registry.drivers[i]->name) {
            LOG_E("Unable to register FS driver %s. FS driver of the same name is already registered.\n", driver->name);
            return -EPERM;
        }
    }

    fs_driver_registry.drivers[fs_driver_registry.count++] = driver;
    LOG_I("Registered new FS driver: %s\n", driver->name);
    return 0;
}


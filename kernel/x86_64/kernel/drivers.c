/* File: drivers.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_DRIVERS
#define CURRENT_LOG_NAME "DRIVERS"

#include <kernel/drivers.h>
#include <common/logging.h>
#include <kernel/errno.h>

pci_driver_registry_t pci_driver_registry;

/**
 * @brief Registers a driver to the pci driver registry. Allows auto matching during PCI discovery.
 * @param driver The actual pci driver struct. 
 * returns 0 on success, EPERM if registry full. 
 */
int drivers_pci_register(pci_driver_t driver) {
    if (pci_driver_registry.count >= MAX_PCI_DRIVERS) {
        LOG_E("Unable to register PCI driver %s. Maximum PCI drivers registered reached.\n", driver.name);
        return -EPERM;
    }

    pci_driver_registry.drivers[pci_driver_registry.count++] = driver;
    LOG_I("Registered new PCI driver: %s\n", driver.name);
    return 0;
}


/* File: drivers.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_DRIVERS
#define CURRENT_LOG_NAME "DRIVERS"

#include <kernel/drivers.h>
#include <common/logging.h>

pci_driver_registry_t pci_driver_registry;

int drivers_pci_register(pci_driver_t driver) {
    if (pci_driver_registry.count >= MAX_PCI_DRIVERS) {
        LOG_E("Unable to register PCI driver %s. Maximum PCI drivers registered reached.\n", driver.name);
        return -1;
    }

    pci_driver_registry.drivers[pci_driver_registry.count++] = driver;
    LOG_I("Registered new PCI driver: %s\n", driver.name);
    return 0;
}


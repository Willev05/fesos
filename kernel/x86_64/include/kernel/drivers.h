/* File: drivers.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include <buses/pci.h>

//PCI driver section
#define MAX_PCI_DRIVERS 64

typedef struct {
    pci_driver_t drivers[MAX_PCI_DRIVERS];
    uint64_t count;
} pci_driver_registry_t;

extern pci_driver_registry_t pci_driver_registry;

int drivers_pci_register(pci_driver_t driver);
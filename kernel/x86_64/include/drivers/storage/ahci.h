/* File: ahci.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "../../buses/pci.h"

int ahci_init_device(pci_device_t *pci_device);
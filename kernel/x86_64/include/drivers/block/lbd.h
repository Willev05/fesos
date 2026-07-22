/* File: lbd.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

typedef int (*lbd_driver_read_fn) (void *driver_data, uint64_t lba, uint64_t count, void *buffer);
typedef int (*lbd_driver_write_fn) (void *driver_data, uint64_t lba, uint64_t count, const void *buffer);

typedef struct {
    lbd_driver_read_fn read;
    lbd_driver_write_fn write;
} lbd_driver_api_t;

void lbd_register_drive(void *driver_data, lbd_driver_api_t *driver_api);
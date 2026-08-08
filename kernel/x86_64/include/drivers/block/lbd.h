/* File: lbd.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#define LBD_FLAG_W 0x1
#define LBD_FLAG_F 0x2

struct _lbd_logical_drive;
typedef struct _lbd_logical_drive lbd_logical_drive_t;

typedef int (*lbd_driver_read_fn) (lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, void *buffer);
typedef int (*lbd_driver_write_fn) (lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, const void *buffer);
typedef int (*lbd_driver_flush_fn) (lbd_logical_drive_t *logical_drive);

typedef struct {
    lbd_driver_read_fn read;
    lbd_driver_write_fn write;
    lbd_driver_flush_fn flush;
} lbd_driver_api_t;

typedef struct {
    uint64_t total_sectors;
    uint32_t logical_sector_size_bytes;
    uint32_t physical_sector_size_bytes;
    uint16_t logical_alignment_offset;
    uint16_t max_sectors_per_transfer;
    uint16_t flags;
} lbd_device_info_t;

struct _lbd_logical_drive {
    const lbd_driver_api_t *driver_api;
    lbd_device_info_t device_info;
    void *driver_data;
    char drive_name[40];
    uint8_t drive_no; //To be filled by the LBD.
};

void lbd_register_drive(lbd_logical_drive_t *logical_drive);
int lbd_read(uint8_t drive_no, uint64_t lba, uint64_t count, void *buffer);
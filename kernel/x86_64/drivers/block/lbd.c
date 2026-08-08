/* File: lbd.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../include/drivers/block/lbd.h"
#include "../../include/common/printf.h"
#include "../../include/kernel/errno.h"

static lbd_logical_drive_t *lbd_drives[256];
static uint8_t next_drive_num = 0;

void lbd_register_drive(lbd_logical_drive_t *logical_drive) {
    uint8_t drive_num = next_drive_num++;
    logical_drive->drive_no = drive_num;
    lbd_drives[drive_num] = logical_drive;
    kprintf("[LBD] Registered new drive: Drive number: %u, Drive name (from driver): %s\n", drive_num, logical_drive->drive_name);
}

int lbd_read(uint8_t drive_no, uint64_t lba, uint64_t count, void *buffer) {
    lbd_logical_drive_t *logical_drive = lbd_drives[drive_no];
    //Check if the no is valid
    if (!logical_drive) return -EINVAL;
    //Check if the count is valid
    if (count > logical_drive->device_info.max_sectors_per_transfer) return -EINVAL;
    //Check if the lba is valid
    if (lba >= logical_drive->device_info.total_sectors) return -EINVAL;

    //Now, buffer check. We wanna check to see if it is a legal address, and contains WRITEABLE flag.
    
}
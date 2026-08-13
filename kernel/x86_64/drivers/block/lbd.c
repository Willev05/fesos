/* File: lbd.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../include/drivers/block/lbd.h"
#include "../../include/common/printf.h"
#include "../../include/kernel/errno.h"
#include "../../include/memory/memory.h"
#include "../../include/common/stdstr.h"

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
    kprintf("[LBD] Processing read request for drive %u.\n", drive_no);
    //Check if the no is valid
    if (!logical_drive) return -EINVAL;
    //Check if the count is valid
    if (count > logical_drive->device_info.max_sectors_per_transfer) return -EINVAL;
    //Check if the lba is valid
    if (lba >= logical_drive->device_info.total_sectors) return -EINVAL;

    uint64_t buffer_vaddr = (uint64_t)buffer;

    //Now, buffer check. We wanna check to see if it is a legal address, and contains WRITEABLE flag.
    if (!vmm_pin_pages(buffer_vaddr, count * logical_drive->device_info.logical_sector_size_bytes, 1)) {
        kprintf("[LBD] Invalid buffer information!\n");
    }

    //Check alignment of the v_address. If not even (word aligned) then we need to use bounce buffers.
    if ((uint64_t)buffer & 1ULL) {
        kprintf("[LBD] User buffer is not word aligned. Falling back to bounce buffer.\n");
        //We need to request the pages. In case of physical fragmentation, we ask in 1 pageat a time. To reduce delays, bypass the vma. Ask pmm directly.
        size_t page_count = count * logical_drive->device_info.logical_sector_size_bytes / 0x1000;
        dma_scatter_block_t scatter_block = kallocate_scatter_dma(page_count);
        if (!scatter_block.virtual_addr) {
            kprintf("[LBD] Could not allocate scattered bounce buffer. Out of memory.\n");
            return -ENOMEM;
        }

        //Then, call the driver.
        int read_errno = logical_drive->driver_api->read(logical_drive, lba, count, scatter_block.virtual_addr);
        if (read_errno) {
            //Error happened. Free everything and return the same code up the call stack.
            kprintf("[LBD] Driver returned error code. Freeing resources and returning.\n");
            kfree_scatter_dma(scatter_block);
            return read_errno;
        }

        //Now, we need to copy the buffer over.
        memcpy(buffer, scatter_block.virtual_addr, count * logical_drive->device_info.logical_sector_size_bytes);
        kfree_scatter_dma(scatter_block);
        kprintf("[LBD] Read using bounce buffer finished.\n");
        return 0;
    }

    else {
        return logical_drive->driver_api->read(logical_drive, lba, count, buffer);
        kprintf("[LBD] Read finished.\n");
    }

    
}
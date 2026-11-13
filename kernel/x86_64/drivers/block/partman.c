/* File: partman.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_STORAGE
#define CURRENT_LOG_NAME "partman"
#define PARTITION_ARRAY_SIZE 256

#include <drivers/block/partman.h>
#include <memory/kmalloc.h>
#include <common/logging.h>
#include <common/stdstr.h>

static disk_partition_t *partitions[PARTITION_ARRAY_SIZE];

/**
 * @brief Parses a drive for partitions. Will then register partitions as their own lbd drive.
 * @param drive A pointer to the drive to register as a partition.
 */
void partman_parse_drive(lbd_logical_drive_t *drive) {
    //So far, only GPT is handled, MBR is not. TODO is add this.
    uint32_t logical_sector_size = drive->device_info.logical_sector_size_bytes;

    //To begin, we will read in the first sector at LBA 0. We want to check the first partition entry, which should be protective GPT partition.
    volatile mbr_partition_table_t *mbr_table = (volatile mbr_partition_table_t*)kmalloc(logical_sector_size);
    int lbd_read_return = lbd_read(drive->drive_no, 0, 1, mbr_table);
    if (lbd_read_return != 0) {
        LOG_E("Could not parse disk %s: LBD read error. Check logs.\n", drive->name);
        goto cleanup;
    }
    if (mbr_table->partition_records[0].partition_type != 0xEE) {
        LOG_E("Could not parse disk %s: Partition not GPT protective type! MBR is not supported...\n", drive->name);
        goto cleanup;
    }

    //Now, we know this is protective, so we will check the LBA 1 for the header.
    //Reuse the allocated memory, since we dont need the MBR anymore.
    volatile gpt_header_t *gpt_header = (volatile gpt_header_t*)mbr_table;
    lbd_read_return = lbd_read(drive->drive_no, 1, 1, gpt_header);
    if (lbd_read_return != 0) {
        LOG_E("Could not parse disk %s: LBD read error. Check logs.\n", drive->name);
        goto cleanup;
    }

    //We want the signature to be the right ASCII string.
    if (str_cmp(gpt_header->signature, "EFI PART") != 0) {
        LOG_E("Could not parse disk %s: GPT header signature invalid.\n", drive->name);
        goto cleanup;
    }
    LOG_D("Disk %s contains valid GPT table with %u partition entries.\n", drive->name, gpt_header->partition_entry_count);

cleanup:
    kfree(mbr_table);
    kfree(gpt_header);
    return;
}
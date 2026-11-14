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

static void gpt_utf16_to_ascii(const uint16_t *utf16_src, char *ascii_dest, size_t n);

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
    LOG_D("Disk %s contains valid GPT table with %u partition entries, each %u bytes.\n", drive->name, gpt_header->partition_entry_count, gpt_header->partition_entry_size);

    //We start parsing the partition entry array.
    volatile gpt_partition_entry_t *partition_entry_ptr = (volatile gpt_partition_entry_t*)kmalloc(gpt_header->partition_entry_size * gpt_header->partition_entry_count);
    lbd_read(drive->drive_no, gpt_header->partition_entry_lba, gpt_header->partition_entry_size * gpt_header->partition_entry_count / drive->device_info.logical_sector_size_bytes, partition_entry_ptr);

    //Loop through them.
    volatile gpt_partition_entry_t *gpt_partition_entry_current = partition_entry_ptr;
    uint8_t *gpt_partition_entry_current_byte_ptr = (uint8_t*)gpt_partition_entry_current;
    gpt_guid_t empty_guid = EMPTY_GUID;
    for (int i = 0; i < gpt_header->partition_entry_count; i++) {
        //If 0, then partition entry is unused.
        if (memcmp(&gpt_partition_entry_current->partition_type_guid, &empty_guid, 16) != 0){
            LOG_D("Disk %s found partition at entry %lu. Start LBA: %lu, End LBA: %lu\n", drive->name, i + 1, gpt_partition_entry_current->start_lba, gpt_partition_entry_current->end_lba);
            //Create the LBD representing the partition.
            lbd_logical_drive_t *partition_drive = (lbd_logical_drive_t*)kmalloc(sizeof(lbd_logical_drive_t));
            partition_drive->device_info.total_sectors = gpt_partition_entry_current->end_lba - gpt_partition_entry_current->start_lba + 1;
            partition_drive->device_info.start_lba = gpt_partition_entry_current->start_lba;
            partition_drive->device_info.logical_sector_size_bytes = logical_sector_size;
            partition_drive->device_info.physical_sector_size_bytes = drive->device_info.physical_sector_size_bytes;
            partition_drive->device_info.logical_alignment_offset = drive->device_info.logical_alignment_offset;
            partition_drive->device_info.max_sectors_per_transfer = drive->device_info.max_sectors_per_transfer;
            partition_drive->device_info.flags = drive->device_info.flags;
            partition_drive->parent_drive = drive;

            str_ncpy("Unknown Partition Type", partition_drive->model_string, sizeof(partition_drive->model_string));
            gpt_utf16_to_ascii(gpt_partition_entry_current->partition_name, partition_drive->name, sizeof(partition_drive->name));
        
            lbd_register_drive(partition_drive);
        }
        
        //Next partition entry code.
        gpt_partition_entry_current_byte_ptr += gpt_header->partition_entry_size;
        gpt_partition_entry_current = (volatile gpt_partition_entry_t*)gpt_partition_entry_current_byte_ptr;
    }

cleanup:
    kfree(mbr_table);
    kfree(gpt_header);
    kfree(partition_entry_ptr);
    return;
}

static void gpt_utf16_to_ascii(const uint16_t *utf16_src, char *ascii_dest, size_t n) {
    size_t i = 0;
    //Leave room for the null terminator (-1)
    while (i < (n - 1)) {
        uint16_t c = utf16_src[i];
        
        //Stop at null terminator
        if (c == 0) {
            break;
        }

        //If it's standard ASCII, take the lower byte. 
        //If it's outside ASCII range, substitute with '?'.
        if (c < 128) {
            ascii_dest[i] = (char)c;
        } else {
            ascii_dest[i] = '?'; 
        }
        
        i++;
    }
    
    //Ensure null-termination
    ascii_dest[i] = '\0';
}
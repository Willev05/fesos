/* File: partman.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Note: GPT table definitions derived from OSDev wiki: https://wiki.osdev.org/GPT */
/* Note: GPT and MBR table definitions derived from UEFI: https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html */
#pragma once

#include <drivers/block/lbd.h>

typedef int (*fs_probe_t)(lbd_logical_drive_t *drive);

typedef struct {
    uint8_t boot_indicator;
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t partition_type;
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed)) mbr_partition_record_t;

typedef struct {
    uint8_t bootstrap_code[440];
    uint32_t disk_signature;
    uint16_t reserved1;
    mbr_partition_record_t partition_records[4];
    uint16_t boot_signature;
} __attribute__((packed)) mbr_partition_table_t;

typedef struct {
    uint32_t Data1; //Little endian
    uint16_t Data2; //Little endian
    uint16_t Data3; //Little endian
    uint8_t Data4[8]; //Big endian
} gpt_guid_t;

typedef struct {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved1;
    uint64_t this_lba; //This header's LBA on disk.
    uint64_t alternate_lba; //Alternate/backup header's LBA on disk.
    uint64_t first_usable_lba; //First usable for a partition desribed by GPT partition entry.
    uint64_t last_usable_lba; //Last usable for a partition desribed by GPT partition entry. 
    gpt_guid_t disk_guid;
    uint64_t partition_entry_lba; //Lba of the first entry in the partition entry array.
    uint32_t partition_entry_count; 
    uint32_t partition_entry_size; //Size of each partition entry in bytes. Will be multiple of 128
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    gpt_guid_t partition_type_guid;
    gpt_guid_t partition_unique_guid;
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} __attribute__((packed)) gpt_partition_entry_t;

typedef struct _disk_partition{
    lbd_logical_drive_t *lbd;
    char fs_name[16];
    uint8_t is_root_candidate;
    char volume_label[36];
    uint8_t type_guid[16];
} disk_partition_t;

void partman_parse_drive(lbd_logical_drive_t *drive);

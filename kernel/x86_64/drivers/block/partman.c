/* File: partman.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define CURRENT_LOG_SYS LOG_SYS_STORAGE
#define CURRENT_LOG_NAME "partman"
#define PARTITION_ARRAY_SIZE 256

#include <drivers/block/partman.h>

static disk_partition_t *partitions[PARTITION_ARRAY_SIZE];

/**
 * @brief Parses a drive for partitions. Will then register partitions as their own lbd drive.
 * @param drive A pointer to the drive to register as a partition.
 */
void partman_parse_drive(lbd_logical_drive_t *drive) {

}
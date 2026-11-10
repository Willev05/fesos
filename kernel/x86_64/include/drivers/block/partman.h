/* File: partman.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/block/lbd.h>

typedef int (*fs_probe_t)(lbd_logical_drive_t *drive);

typedef struct _disk_partition{
    lbd_logical_drive_t *lbd;
    char fs_name[16];
    uint8_t is_root_candidate;
    char volume_label[36];
    uint8_t type_guid[16];
} disk_partition_t;

void partman_parse_drive(lbd_logical_drive_t *drive);

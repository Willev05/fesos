/* File: ramfs.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <kernel/vfs.h>

void ramfs_init();
vfs_node_t *ramfs_get_fs();
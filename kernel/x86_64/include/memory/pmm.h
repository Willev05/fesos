/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

void pmm_init(uint64_t bi_v);
void *pmm_allocate_frames(uint64_t count, uint64_t alignment);
void pmm_free_frames(void *start_address, uint64_t count);
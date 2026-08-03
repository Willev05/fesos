/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>
#include "../common/stdtypes.h"

typedef enum {
    STR_SUCCESS = 0,
    STR_ERR_INVALID_ARG = -1,
    STR_ERR_BUFFER_TOO_SMALL = -2
} str_status_t;

int ultoa(uint64_t number, char *buffer, size_t size);
int ultox(uint64_t number, char *buffer, size_t size);
int str_reverse(char *str, size_t start_index, size_t end_index);
size_t str_len(char *str);
void *memset(void *start, uint8_t pattern, size_t size);
void *volatile_memset(volatile void *start, uint8_t pattern, size_t size);
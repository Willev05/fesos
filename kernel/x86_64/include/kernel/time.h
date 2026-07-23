/* File: time.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "../drivers/io.h"

void tsc_timer_init();
uint64_t tsc_timer_get_ms();
void tsc_sleep_ms(uint64_t ms);
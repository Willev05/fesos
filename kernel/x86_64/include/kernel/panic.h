/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

void kernel_panic(const char *file, int line, const char *msg, ...);

#define PANIC(msg, ...) kernel_panic(__FILE_NAME__, __LINE__, msg, ##__VA_ARGS__);
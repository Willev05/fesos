/* File: printf.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdarg.h>
#include <common/stdtypes.h>

int kprintf(const char *fmt, ...);
int vkprintf(const char *fmt, va_list args);
size_t snkprintf(char *str, size_t size, const char *format, ...);
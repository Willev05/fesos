/* File: logging.h */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdarg.h>

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_DEBUG
} log_level_t;

//The core function.
void log_message(log_level_t log_level, const char *file, const char *fmt, ...);

//The easy macros to sub file and log_level automatically.
#define LOG_I(fmt, ...) log_message(LOG_INFO, __FILE_NAME__, fmt, ##__VA_ARGS__);
#define LOG_W(fmt, ...) log_message(LOG_WARN, __FILE_NAME__, fmt, ##__VA_ARGS__);
#define LOG_E(fmt, ...) log_message(LOG_ERROR, __FILE_NAME__, fmt, ##__VA_ARGS__);
#define LOG_D(fmt, ...) log_message(LOG_DEBUG, __FILE_NAME__, fmt, ##__VA_ARGS__);
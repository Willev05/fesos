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

typedef enum {
    LOG_SYS_INIT = 1,
    LOG_SYS_PMM = (1 << 1),
    LOG_SYS_VMM = (1 << 2), //Handles VMM and VMA
    LOG_SYS_STORAGE = (1 << 3), // Handles the storage drivers, and LBD.
    LOG_SYS_KALLOC = (1 << 4), //Handles kmalloc and such functions.

    LOG_SYS_NONE = 0,
    LOG_SYS_ALL = 0xFFFFFFFF
} log_subsystem_t;

//Control functions.
void log_enable_subsystem_debug(log_subsystem_t subsystem_bit);
void log_disable_subsystem_debug(log_subsystem_t subsystem_bit);

//The core function.
void log_message(log_level_t log_level, log_subsystem_t subsystem, const char *component_name, const char *fmt, ...);

//The easy macros to sub file and log_level automatically.
#ifndef CURRENT_LOG_SYS
    #define CURRENT_LOG_SYS LOG_SYS_INIT
#endif

#ifndef CURRENT_LOG_NAME
    #define CURRENT_LOG_NAME "INIT"
#endif

#define LOG_I(fmt, ...) log_message(LOG_INFO, CURRENT_LOG_SYS, CURRENT_LOG_NAME, fmt, ##__VA_ARGS__);
#define LOG_W(fmt, ...) log_message(LOG_WARN, CURRENT_LOG_SYS, CURRENT_LOG_NAME, fmt, ##__VA_ARGS__);
#define LOG_E(fmt, ...) log_message(LOG_ERROR, CURRENT_LOG_SYS, CURRENT_LOG_NAME, fmt, ##__VA_ARGS__);
#define LOG_D(fmt, ...) log_message(LOG_DEBUG, CURRENT_LOG_SYS, CURRENT_LOG_NAME, fmt, ##__VA_ARGS__);
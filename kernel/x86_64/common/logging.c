/* File: logging.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../include/common/logging.h"
#include "../include/common/printf.h"
#include "../include/kernel/time.h"

static uint32_t subsystem_mask = 0;

void log_enable_subsystem_debug(log_subsystem_t subsystem_bit) {
    subsystem_mask |= subsystem_bit;
}

void log_disable_subsystem_debug(log_subsystem_t subsystem_bit) {
    subsystem_mask &= ~subsystem_bit;
}

uint8_t log_is_subsystem_debug_enabled(log_subsystem_t subsystem_bit) {
    return subsystem_mask & subsystem_bit;
}

void log_message(log_level_t log_level, log_subsystem_t subsystem, const char *component_name, const char *fmt, ...) {
    //Check that if debug, subsystem is marked as enabled.
    if (log_level == LOG_DEBUG && !log_is_subsystem_debug_enabled(subsystem)) return;

    //Time stuff.
    uint64_t millisecond = tsc_timer_get_ms();
    uint32_t hour = (uint32_t)(millisecond / 3600000ULL);
    uint8_t minutes = (uint8_t)((millisecond % 3600000ULL) / 60000ULL); 
    uint8_t seconds = (uint8_t)((millisecond % 60000ULL) / 1000ULL);
    uint16_t ms = (uint16_t)(millisecond % 1000ULL);

    //Print a timestamp and filename. The level + rest of message handled later.
    kprintf("[%u:%u:%u:%u] [%s-", hour, minutes, seconds, ms, component_name);
    
    char *log_level_txt;
    
    switch (log_level) {
        case LOG_INFO:
            log_level_txt = "INFO";
            break;
        case LOG_WARN:
            log_level_txt = "WARN";
            break;
        case LOG_ERROR:
            log_level_txt = "ERROR";
            break;
        case LOG_DEBUG:
            log_level_txt = "DEBUG";
            break;
    }

    kprintf("%s] ", log_level_txt);
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
}
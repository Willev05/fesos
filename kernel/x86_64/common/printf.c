/* File: printf.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../include/common/printf.h"
#include "../include/drivers/serial.h"
#include "../include/common/stdstr.h"

int kprintf(const char* fmt, ...) {
    static char buffer[32];
    va_list args;
    va_start(args, fmt);

    //We loop through the entire format string.
    for (const char* p = fmt; *p != '\0'; p++) {
        //If just a normal char, simply print it.
        if (*p != '%') {
            serial_putc(*p);
            continue;
        }

        //If a %, skip it!
        p++;

        //We check to see if the next char is l, if so, its then its long (64 bit).
        uint8_t is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
        }

        //Now, simply look at the formats and get them from the va_args.
        switch (*p) {
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_putc(c);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) break;
                serial_puts(s);
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_long == 1) val = va_arg(args, uint64_t);
                else val = va_arg(args, unsigned int);
                ultoa(val, buffer, 21);
                serial_puts(buffer);
                break;
            }
            case 'x': {
                uint64_t val;
                if (is_long == 1) val = va_arg(args, uint64_t);
                else val = va_arg(args, unsigned int);
                ultox(val, buffer, 21);
                serial_puts(buffer);
                break;
            }
            case '%': {
                serial_putc('%');
                break;
            }
            default: {
                //Unknown, so just print the thing.
                serial_putc('%');
                serial_putc(*p);
                break;
            }
        }
    }

    va_end(args);
    return 0;
}
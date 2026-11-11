/* File: printf.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <common/printf.h>
#include <drivers/serial.h>
#include <common/stdstr.h>
#include <common/math.h>

/**
 * @brief Printf for the kernel using varargs.
 * @param fmt The format string.
 * @param ... The additional values to sub into the format string.
 * @return 0 on success.
 */
int kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
    return 0;
}

/**
 * @brief Printf for the kernel using a va_list.
 * @param fmt The format string.
 * @param args The argument list.
 * @return 0 on success.
 */
int vkprintf(const char *fmt, va_list args) {
    static char buffer[32];

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
    return 0;
}

/**
 * @brief Printf but writes to `str` instead of stdout. 
 * @param str Pointer to string buffer. Must be at least `size` bytes long. Includes null term.
 * @param size The amount of characters/bytes this function is allowed to write into `str`.
 * @param format The format string.
 * @param ... The additional values to sub into the format string.
 * @return Number of characters printed into `str`.
 */
size_t snkprintf(char *str, size_t size, const char *fmt, ...) {
    static char buffer[32];

    va_list args;
    va_start(args, fmt);

    size_t str_index = 0;

    //We loop through the entire format string.
    for (const char* p = fmt; *p != '\0'; p++) {
        //Ensure there is enough space in the str destination.
        if (str_index >= size) {
            str[str_index - 1] = '\0';
            return str_index;
        }

        //If just a normal char, simply print it.
        if (*p != '%') {
            str[str_index++] = *p;
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
                str[str_index++] = c;
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) break;
                serial_puts(s);
                size_t space_remaining = MIN(str_len(s), size - str_index);
                str_ncpy(s, str + str_index, space_remaining);
                str_index += space_remaining;
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_long == 1) val = va_arg(args, uint64_t);
                else val = va_arg(args, unsigned int);
                ultoa(val, buffer, 21);
                size_t space_remaining = MIN(str_len(buffer), size - str_index);
                str_ncpy(buffer, str + str_index, space_remaining);
                str_index += space_remaining;
                break;
            }
            case 'x': {
                uint64_t val;
                if (is_long == 1) val = va_arg(args, uint64_t);
                else val = va_arg(args, unsigned int);
                ultox(val, buffer, 21);
                size_t space_remaining = MIN(str_len(buffer), size - str_index);
                str_ncpy(buffer, str + str_index, space_remaining);
                str_index += space_remaining;
                break;
            }
            case '%': {
                str[str_index++] = '%';
                break;
            }
            default: {
                //Unknown, so just print the thing.
                str[str_index++] = '%';
                if (str_index >= size) {
                    str[str_index - 1] = '\0';
                    return str_index;
                } 
                str[str_index++] = *p;
                break;
            }
        }
    }
    va_end(args);
    return str_index;
}
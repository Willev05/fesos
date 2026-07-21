/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../include/common/stdstr.h"

int ultoa(uint64_t number, char *buffer, size_t size) {
    //Null buffer check.
    if (buffer == 0) return STR_ERR_INVALID_ARG;

    //Check to see if the buffer can handle at least 0. It needs to be at least 2 big, since 0 and null term.
    if (size < 2) return STR_ERR_BUFFER_TOO_SMALL;

    //0 Check seperate
    if (number == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return STR_SUCCESS;
    }

    size_t i = 0;
    //Null terminator, so size - 1
    while (i < size - 1 && number > 0){
        buffer[i++] = 0x30 + (number % 10);
        number /= 10;
    }

    buffer[i] = '\0';
    
    //If number is still not 0, that means provided buffer was too small.
    if (number > 0) return STR_ERR_BUFFER_TOO_SMALL;

    //We reverse the string, since it is reversed for now.
    str_reverse(buffer, 0, str_len(buffer));

    return STR_SUCCESS;
}

int ultox(uint64_t number, char *buffer, size_t size) {
    static char hex_chars[] = {
        0x30, //0
        0x31, //1
        0x32, //2
        0x33, //3
        0x34, //4
        0x35, //5
        0x36, //6
        0x37, //7
        0x38, //8
        0x39, //9
        0x41, //A
        0x42, //B
        0x43, //C
        0x44, //D
        0x45, //E
        0x46  //F
    };

    //Null buffer check.
    if (buffer == 0) return STR_ERR_INVALID_ARG;

    //Check to see if the buffer can handle at least 0. It needs to be at least 4 big, since 0, null term and 0x prefix.
    if (size < 4) return STR_ERR_BUFFER_TOO_SMALL;
    buffer[0] = '0';
    buffer[1] = 'x';

    //Handle 0 as input.
    if (number == 0) {
        buffer[2] = '0';
        buffer[3] = '\0';
        return STR_SUCCESS;
    }

    size_t i = 2;
    //Null terminator, so size - 1
    while (i < size - 1 && number > 0) {
        buffer[i++] = hex_chars[number & 0xF];
        number = number >> 4;
    }

    buffer[i] = '\0';

    //If number is still not 0, that means provided buffer was too small.
    if (number > 0) return STR_ERR_BUFFER_TOO_SMALL;

    //We need to reverse the string.
    str_reverse(buffer, 2, str_len(buffer));

    return STR_SUCCESS;
}

//Start inclusive, end exclusive.
int str_reverse(char *str, size_t start_index, size_t end_index) {
    if (str == NULL) return STR_ERR_INVALID_ARG;

    size_t left_ptr = start_index;
    size_t right_ptr = end_index - 1;

    while (left_ptr <= right_ptr) {
        if (left_ptr == right_ptr) {
            break;
        }
        char temp = str[left_ptr];
        str[left_ptr++] = str[right_ptr];
        str[right_ptr--] = temp;
    }

    return STR_SUCCESS;
}

size_t str_len(char *str) {
    size_t len = 0;
    while (str[len++]);
    return len - 1;
}

/**
 * @brief Sets the memory at location 'start' to pattern 'pattern' over 'size' bytes.
 * @param start A pointer to starting memory location.
 * @param pattern The byte-pattern to repeat throughout the memory area.
 * @param size The size in bytes of the memory area.
 * @return The same pointer passed into start.
 */
void *memset(void *start, uint8_t pattern, size_t size) {
    //This fuction will attempt to write the pattern in stacks of 64 bits for most efficiency in C.
    //We however need to start by applying the pattern to the head until we reach 8-byte alignment.
    uint8_t *ptr8 = (uint8_t*)start;
    while (((uint64_t)ptr8 & 0x7) && 0 < size) {
        *ptr8++ = pattern;
        size--;
    }

    //Now, we handle the middle portion, which is the faster 64-bit writes.
    uint64_t pattern64 = (uint64_t)pattern;
    pattern64 |= pattern64 << 8;
    pattern64 |= pattern64 << 16;
    pattern64 |= pattern64 << 32;

    //The pointer should be alligned now.
    uint64_t *ptr64 = (uint64_t*)ptr8;
    while (size >= 8) {
        *ptr64++ = pattern64;
        size -= 8;
    }

    //Lastly, handle the tail, if unaligned.
    ptr8 = (uint8_t*)ptr64;
    while (size > 0) {
        *ptr8++ = pattern;
        size--;
    }

    return start;
}
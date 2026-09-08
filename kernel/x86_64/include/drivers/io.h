/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

/**
 * @brief Writes a byte to an IO port.
 * @param port The 16-bit port to write to.
 * @param byte The byte to write.
 */
static inline void outb(uint16_t port, uint8_t byte) {
    __asm__ volatile ("outb %0, %1" : : "a"(byte), "Nd"(port));
}

/**
 * @brief Reads a byte from an IO port.
 * @param port The 16-bit port to read from.
 * @return The byte read.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Writes a word to an IO port.
 * @param port The 16-bit port to write to.
 * @param word The word to write.
 */
static inline void outw(uint16_t port, uint16_t word) {
    __asm__ volatile ("outw %0, %1" : : "a"(word), "Nd"(port));
}

/**
 * @brief Reads a word from an IO port.
 * @param port The 16-bit port to read from.
 * @return The word read.
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Writes a double-word (long) to an IO port.
 * @param port The 16-bit port to write to.
 * @param long_val The double-word (long) to write.
 */
static inline void outl(uint16_t port, uint32_t long_val) {
    __asm__ volatile ("outl %0, %1" : : "a"(long_val), "Nd"(port));
}

/**
 * @brief Reads a double-word (long) from an IO port.
 * @param port The 16-bit port to read from.
 * @return The double-word (long) read.
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Issues a PAUSE instrutction to hint the CPU that it is in a spin lock.
 * Decreases power consumption.
 */
static inline void cpu_relax() {
    __asm__ volatile ("pause" ::: "memory");
}

/**
 * @brief Reads the 64-bit Time Stamp Counter (TSC).
 * 
 * @return Total elapsed CPU cycles since CPU start.
 */
static inline uint64_t rdtsc(){
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
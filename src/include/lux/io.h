/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Inline port I/O utilities for interacting with hardware.
 */
#pragma once

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read an 8-bit value from the specified x86 I/O port.
 *
 * @param port I/O port number to read from.
 * @returns The 8-bit value read from the port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a 16-bit value to an x86 I/O port.
 *
 * @param port  I/O port number to write to.
 * @param value  16-bit value to send to the port.
 */
static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read a 16-bit value from the specified x86 I/O port.
 *
 * @param port I/O port number to read from.
 * @returns The 16-bit value read from the I/O port.
 */
static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: PIT (Programmable Interval Timer) and timer interrupt handling.
 */
#pragma once

#include <stdint.h>

/**
 * Initialize the PIT (8253/8254 Programmable Interval Timer).
 * 
 * Sets up IRQ0 (timer interrupt) to fire at approximately 1000 Hz (1 ms intervals).
 * This enables preemptive multitasking scheduling.
 */
void pit_init(void);

/**
 * Get the current system timer tick count.
 * 
 * @returns Number of milliseconds elapsed since system startup.
 */
uint32_t pit_get_ticks(void);

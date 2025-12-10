#pragma once

#include <stdint.h>

/**
 * Busy-wait for approximately the requested number of milliseconds.
 * Accuracy depends on CPU speed because the kernel lacks a hardware timer.
 */
void sleep_ms(uint32_t milliseconds);

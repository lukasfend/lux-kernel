/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: VGA text terminal control API for console output.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

void tty_init(uint8_t color);
void tty_set_color(uint8_t color);
void tty_putc(char c);
void tty_write(const char *data, size_t len);
void tty_write_string(const char *str);

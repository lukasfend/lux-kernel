/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: VGA text terminal control API for console output.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct tty_cell {
    char character;
    uint8_t color;
};

void tty_init(uint8_t color);
void tty_set_color(uint8_t color);
void tty_putc(char c);
void tty_write(const char *data, size_t len);
void tty_write_string(const char *str);
void tty_clear(void);

size_t tty_rows(void);
size_t tty_cols(void);
void tty_write_cell(size_t row, size_t col, char c, uint8_t color);
char tty_get_cell_character(size_t row, size_t col);
uint8_t tty_get_cell_color(size_t row, size_t col);
void tty_set_cursor_position(size_t row, size_t col);
void tty_get_cursor_position(size_t *row, size_t *col);
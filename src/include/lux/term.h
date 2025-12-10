/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Off-screen terminal surfaces for full-screen text applications.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/tty.h>

struct term_surface {
    size_t rows;
    size_t cols;
    size_t cursor_row;
    size_t cursor_col;
    uint8_t default_color;
    struct tty_cell *cells;
};

struct term_surface *term_surface_create(uint8_t color);
void term_surface_destroy(struct term_surface *surface);
bool term_surface_resize(struct term_surface *surface, size_t rows, size_t cols);
void term_surface_clear(struct term_surface *surface, char fill_char);
void term_surface_set_cursor(struct term_surface *surface, size_t row, size_t col);
void term_surface_draw_char(struct term_surface *surface, size_t row, size_t col, char c, uint8_t color);
void term_surface_write_string(struct term_surface *surface, size_t row, size_t col, const char *text, uint8_t color);
void term_surface_fill_rect(struct term_surface *surface, size_t row, size_t col, size_t height, size_t width, char c, uint8_t color);
void term_surface_scroll_region(struct term_surface *surface, size_t top_row, size_t bottom_row, int delta_rows, char fill_char);
void term_surface_flush(const struct term_surface *surface);
void term_surface_flush_region(const struct term_surface *surface, size_t row, size_t col, size_t height, size_t width);
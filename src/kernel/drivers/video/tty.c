/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: 640x480x16 planar VGA terminal with software rendered glyphs.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <lux/io.h>
#include <lux/tty.h>

#include "font8x8_basic.h"

#define SCREEN_WIDTH          640u
#define SCREEN_HEIGHT         480u
#define CELL_WIDTH            8u
#define CELL_HEIGHT           16u
#define VGA_BYTES_PER_SCANLINE (SCREEN_WIDTH / 8u)
#define VGA_MEMORY_SIZE       0x10000u

#define TTY_COLS (SCREEN_WIDTH / CELL_WIDTH)
#define TTY_ROWS (SCREEN_HEIGHT / CELL_HEIGHT)
#define CURSOR_INVALID ((size_t)(-1))

#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA  0x3C5
#define VGA_GC_INDEX  0x3CE
#define VGA_GC_DATA   0x3CF

static volatile uint8_t *const VGA_MEMORY = (volatile uint8_t *)0xA0000u;

static size_t cursor_row;
static size_t cursor_col;
static uint8_t current_color;
static struct tty_cell cells[TTY_ROWS * TTY_COLS];
static size_t cursor_overlay_row = CURSOR_INVALID;
static size_t cursor_overlay_col = CURSOR_INVALID;

static inline size_t tty_cell_index(size_t row, size_t col)
{
    return row * TTY_COLS + col;
}

static inline size_t vga_offset(size_t x, size_t y)
{
    return y * VGA_BYTES_PER_SCANLINE + (x / 8u);
}

static inline uint8_t vga_bit_mask(size_t x)
{
    return (uint8_t)(0x80u >> (x & 7u));
}

static inline void vga_set_map_mask(uint8_t mask)
{
    outb(VGA_SEQ_INDEX, 0x02);
    outb(VGA_SEQ_DATA, mask);
}

static inline void vga_select_read_plane(uint8_t plane)
{
    outb(VGA_GC_INDEX, 0x04);
    outb(VGA_GC_DATA, plane);
}

static void vga_program_palette(void)
{
    static const uint8_t palette[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0x2A}, {0x00, 0x2A, 0x00}, {0x00, 0x2A, 0x2A},
        {0x2A, 0x00, 0x00}, {0x2A, 0x00, 0x2A}, {0x2A, 0x15, 0x00}, {0x2A, 0x2A, 0x2A},
        {0x15, 0x15, 0x15}, {0x15, 0x15, 0x3F}, {0x15, 0x3F, 0x15}, {0x15, 0x3F, 0x3F},
        {0x3F, 0x15, 0x15}, {0x3F, 0x15, 0x3F}, {0x3F, 0x3F, 0x15}, {0x3F, 0x3F, 0x3F}
    };

    outb(0x3C8, 0x00);
    for (size_t i = 0; i < 16; ++i) {
        outb(0x3C9, palette[i][0]);
        outb(0x3C9, palette[i][1]);
        outb(0x3C9, palette[i][2]);
    }
}

static void vga_clear_screen(void)
{
    vga_set_map_mask(0x0F);
    memset((void *)VGA_MEMORY, 0x00, VGA_MEMORY_SIZE);
}

static void vga_set_pixel(size_t x, size_t y, uint8_t color)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
        return;
    }

    color &= 0x0F;
    size_t offset = vga_offset(x, y);
    uint8_t mask = vga_bit_mask(x);

    for (uint8_t plane = 0; plane < 4u; ++plane) {
        uint8_t plane_mask = (uint8_t)(1u << plane);
        vga_set_map_mask(plane_mask);
        vga_select_read_plane(plane);

        volatile uint8_t *dst = VGA_MEMORY + offset;
        uint8_t value = *dst;
        if (color & plane_mask) {
            value |= mask;
        } else {
            value &= (uint8_t)~mask;
        }
        *dst = value;
    }
}

static uint8_t glyph_row_bits(uint8_t ch, size_t scanline)
{
    if (ch >= 128) {
        ch = '?';
    }
    return font8x8_basic[ch][scanline / 2u];
}

static void tty_draw_glyph(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return;
    }

    size_t idx = tty_cell_index(row, col);
    struct tty_cell cell = cells[idx];
    uint8_t fg = cell.color & 0x0F;
    uint8_t bg = (cell.color >> 4) & 0x0F;
    uint8_t ch = cell.character ? (uint8_t)cell.character : ' ';
    if (ch >= 128) {
        ch = '?';
    }

    size_t base_x = col * CELL_WIDTH;
    size_t base_y = row * CELL_HEIGHT;

    for (size_t y = 0; y < CELL_HEIGHT; ++y) {
        uint8_t bits = glyph_row_bits(ch, y);
        for (size_t x = 0; x < CELL_WIDTH; ++x) {
            uint8_t color = (bits & (uint8_t)(0x80u >> x)) ? fg : bg;
            vga_set_pixel(base_x + x, base_y + y, color);
        }
    }
}

static void tty_draw_cursor_block(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return;
    }

    const struct tty_cell *cell = &cells[tty_cell_index(row, col)];
    uint8_t fg = cell->color & 0x0F;
    uint8_t bg = (cell->color >> 4) & 0x0F;
    uint8_t cursor_color = (fg == bg) ? (uint8_t)(fg ^ 0x0F) : fg;
    cursor_color &= 0x0F;

    size_t base_x = col * CELL_WIDTH;
    size_t base_y = row * CELL_HEIGHT + CELL_HEIGHT - 2u;

    for (size_t y = 0; y < 2u; ++y) {
        for (size_t x = 0; x < CELL_WIDTH; ++x) {
            vga_set_pixel(base_x + x, base_y + y, cursor_color);
        }
    }
}

static void tty_redraw_cursor(void)
{
    if (cursor_overlay_row < TTY_ROWS && cursor_overlay_col < TTY_COLS) {
        tty_draw_glyph(cursor_overlay_row, cursor_overlay_col);
    }

    cursor_overlay_row = cursor_row;
    cursor_overlay_col = cursor_col;

    if (cursor_overlay_row < TTY_ROWS && cursor_overlay_col < TTY_COLS) {
        tty_draw_glyph(cursor_overlay_row, cursor_overlay_col);
        tty_draw_cursor_block(cursor_overlay_row, cursor_overlay_col);
    }
}

static void tty_render_screen(void)
{
    for (size_t row = 0; row < TTY_ROWS; ++row) {
        for (size_t col = 0; col < TTY_COLS; ++col) {
            tty_draw_glyph(row, col);
        }
    }

    cursor_overlay_row = CURSOR_INVALID;
    cursor_overlay_col = CURSOR_INVALID;
    tty_redraw_cursor();
}

static void tty_scroll(void)
{
    if (cursor_row < TTY_ROWS) {
        return;
    }

    size_t overflow = cursor_row - (TTY_ROWS - 1u);
    if (!overflow) {
        overflow = 1u;
    }
    if (overflow > TTY_ROWS) {
        overflow = TTY_ROWS;
    }

    size_t keep_rows = TTY_ROWS - overflow;
    size_t moved_cells = keep_rows * TTY_COLS;
    memmove(cells, cells + overflow * TTY_COLS, moved_cells * sizeof(struct tty_cell));

    struct tty_cell blank = {
        .character = ' ',
        .color = current_color
    };
    size_t start = keep_rows * TTY_COLS;
    size_t total_cells = TTY_ROWS * TTY_COLS;
    for (size_t i = start; i < total_cells; ++i) {
        cells[i] = blank;
    }

    cursor_row = TTY_ROWS - 1u;
    tty_render_screen();
}

void tty_init(uint8_t color)
{
    current_color = color;
    cursor_row = 0;
    cursor_col = 0;
    cursor_overlay_row = CURSOR_INVALID;
    cursor_overlay_col = CURSOR_INVALID;

    vga_program_palette();
    vga_clear_screen();

    struct tty_cell blank = {
        .character = ' ',
        .color = color
    };
    for (size_t i = 0; i < TTY_ROWS * TTY_COLS; ++i) {
        cells[i] = blank;
    }

    tty_render_screen();
}

void tty_set_color(uint8_t color)
{
    current_color = color;
}

void tty_putc(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        ++cursor_row;
        tty_scroll();
        tty_redraw_cursor();
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        tty_redraw_cursor();
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            --cursor_col;
        } else if (cursor_row > 0) {
            --cursor_row;
            cursor_col = TTY_COLS ? TTY_COLS - 1u : 0u;
        }
        size_t idx = tty_cell_index(cursor_row, cursor_col);
        cells[idx].character = ' ';
        cells[idx].color = current_color;
        tty_draw_glyph(cursor_row, cursor_col);
        tty_redraw_cursor();
        return;
    }

    if (cursor_row >= TTY_ROWS) {
        tty_scroll();
    }

    size_t row = cursor_row;
    size_t col = cursor_col;
    size_t idx = tty_cell_index(row, col);
    cells[idx].character = c;
    cells[idx].color = current_color;
    tty_draw_glyph(row, col);

    ++cursor_col;
    if (cursor_col >= TTY_COLS) {
        cursor_col = 0;
        ++cursor_row;
    }

    tty_scroll();
    tty_redraw_cursor();
}

void tty_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        tty_putc(data[i]);
    }
}

void tty_write_string(const char *str)
{
    tty_write(str, strlen(str));
}

void tty_clear(void)
{
    struct tty_cell blank = {
        .character = ' ',
        .color = current_color
    };
    for (size_t i = 0; i < TTY_ROWS * TTY_COLS; ++i) {
        cells[i] = blank;
    }

    cursor_row = 0;
    cursor_col = 0;
    vga_clear_screen();
    tty_render_screen();
}

size_t tty_rows(void)
{
    return TTY_ROWS;
}

size_t tty_cols(void)
{
    return TTY_COLS;
}

void tty_write_cell(size_t row, size_t col, char c, uint8_t color)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return;
    }

    size_t idx = tty_cell_index(row, col);
    cells[idx].character = c;
    cells[idx].color = color;
    tty_draw_glyph(row, col);

    if (row == cursor_row && col == cursor_col) {
        tty_redraw_cursor();
    }
}

char tty_get_cell_character(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return '\0';
    }

    return cells[tty_cell_index(row, col)].character;
}

uint8_t tty_get_cell_color(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return 0;
    }

    return cells[tty_cell_index(row, col)].color;
}

void tty_set_cursor_position(size_t row, size_t col)
{
    if (row >= TTY_ROWS) {
        row = TTY_ROWS ? TTY_ROWS - 1u : 0u;
    }
    if (col >= TTY_COLS) {
        col = TTY_COLS ? TTY_COLS - 1u : 0u;
    }

    cursor_row = row;
    cursor_col = col;
    tty_redraw_cursor();
}

void tty_get_cursor_position(size_t *row, size_t *col)
{
    if (row) {
        *row = cursor_row;
    }
    if (col) {
        *col = cursor_col;
    }
}

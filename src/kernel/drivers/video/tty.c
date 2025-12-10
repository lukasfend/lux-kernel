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

#include "font_ibm_vga_8x16.h"

#define SCREEN_WIDTH           640u
#define SCREEN_HEIGHT          480u
#define CELL_WIDTH             8u
#define CELL_HEIGHT            16u
#define CURSOR_HEIGHT          2u
#define VGA_BYTES_PER_SCANLINE (SCREEN_WIDTH / 8u)
#define VGA_MEMORY_SIZE        0x10000u

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
static uint8_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

static inline size_t tty_cell_index(size_t row, size_t col)
{
    return row * TTY_COLS + col;
}

static inline void vga_set_map_mask(uint8_t mask)
{
    outb(VGA_SEQ_INDEX, 0x02u);
    outb(VGA_SEQ_DATA, mask);
}

static inline void vga_program_palette(void)
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

static inline void vga_clear_screen(void)
{
    vga_set_map_mask(0x0Fu);
    memset((void *)VGA_MEMORY, 0x00, VGA_MEMORY_SIZE);
}

static inline uint8_t bit_reverse(uint8_t value)
{
    value = (uint8_t)(((value & 0x55u) << 1) | ((value & 0xAAu) >> 1));
    value = (uint8_t)(((value & 0x33u) << 2) | ((value & 0xCCu) >> 2));
    return (uint8_t)((value << 4) | (value >> 4));
}

static uint8_t glyph_row_bits(uint8_t ch, size_t scanline)
{
    if (scanline >= CELL_HEIGHT) {
        return 0;
    }
    /* IBM VGA font bytes store pixels MSB->LSB, so flip to match our renderer. */
    return bit_reverse(font_ibm_vga_8x16[ch][scanline]);
}

static void vga_flush_cell(size_t base_x, size_t base_y)
{
    size_t byte_col = base_x / 8u;
    size_t max_row = base_y + CELL_HEIGHT;
    if (max_row > SCREEN_HEIGHT) {
        max_row = SCREEN_HEIGHT;
    }

    for (size_t row = base_y; row < max_row; ++row) {
        size_t fb_offset = row * SCREEN_WIDTH + base_x;
        size_t vga_byte = row * VGA_BYTES_PER_SCANLINE + byte_col;
        uint8_t plane_bytes[4] = {0, 0, 0, 0};

        for (size_t bit = 0; bit < CELL_WIDTH; ++bit) {
            uint8_t color = framebuffer[fb_offset + bit] & 0x0F;
            for (uint8_t plane = 0; plane < 4u; ++plane) {
                plane_bytes[plane] |= (uint8_t)(((color >> plane) & 0x01u) << bit);
            }
        }

        for (uint8_t plane = 0; plane < 4u; ++plane) {
            vga_set_map_mask((uint8_t)(1u << plane));
            VGA_MEMORY[vga_byte] = plane_bytes[plane];
        }
    }

    vga_set_map_mask(0x0Fu);
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
    uint8_t ch = cell.character ? (uint8_t)cell.character : (uint8_t)' ';

    size_t base_x = col * CELL_WIDTH;
    size_t base_y = row * CELL_HEIGHT;

    for (size_t y = 0; y < CELL_HEIGHT && (base_y + y) < SCREEN_HEIGHT; ++y) {
        size_t fb_offset = (base_y + y) * SCREEN_WIDTH + base_x;
        for (size_t x = 0; x < CELL_WIDTH && (base_x + x) < SCREEN_WIDTH; ++x) {
            framebuffer[fb_offset + x] = bg;
        }

        uint8_t bits = glyph_row_bits(ch, y);
        for (size_t x = 0; x < CELL_WIDTH && (base_x + x) < SCREEN_WIDTH; ++x) {
            if (bits & (uint8_t)(0x80u >> x)) {
                framebuffer[fb_offset + x] = fg;
            }
        }
    }

    vga_flush_cell(base_x, base_y);
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
    size_t base_y = row * CELL_HEIGHT;
    size_t start_y = base_y + CELL_HEIGHT - CURSOR_HEIGHT;

    for (size_t y = 0; y < CURSOR_HEIGHT && (start_y + y) < SCREEN_HEIGHT; ++y) {
        size_t fb_offset = (start_y + y) * SCREEN_WIDTH + base_x;
        for (size_t x = 0; x < CELL_WIDTH && (base_x + x) < SCREEN_WIDTH; ++x) {
            framebuffer[fb_offset + x] = cursor_color;
        }
    }

    vga_flush_cell(base_x, base_y);
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

    memset(framebuffer, 0, sizeof(framebuffer));
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

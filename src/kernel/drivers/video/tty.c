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

/**
 * Compute the linear index into the flat cell array for a given row and column.
 * @param row Row index (valid range: 0 .. TTY_ROWS - 1).
 * @param col Column index (valid range: 0 .. TTY_COLS - 1).
 * @returns Linear array index corresponding to the cell at (row, col).
 */
static inline size_t tty_cell_index(size_t row, size_t col)
{
    return row * TTY_COLS + col;
}

/**
 * Select which VGA bitplanes are writable by programming the VGA sequencer map mask.
 * 
 * @param mask Bitmask enabling writes to VGA bitplanes; bit 0 selects plane 0, bit 1 selects plane 1,
 *             bit 2 selects plane 2, and bit 3 selects plane 3 (only the lower 4 bits are meaningful).
 */
static inline void vga_set_map_mask(uint8_t mask)
{
    outb(VGA_SEQ_INDEX, 0x02u);
    outb(VGA_SEQ_DATA, mask);
}

/**
 * Load the standard 16-color VGA palette into the VGA DAC.
 *
 * Programs DAC index 0 and writes 16 RGB entries (6-bit per channel, 0–63)
 * to VGA ports 0x3C8/0x3C9 to establish the terminal's 16-color palette.
 */
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

/**
 * Clear the VGA framebuffer and select all write bitplanes.
 *
 * Programs the VGA sequencer map mask to 0x0F (enabling all four bitplanes)
 * and fills the VGA video memory region starting at VGA_MEMORY with zeros.
 */
static inline void vga_clear_screen(void)
{
    vga_set_map_mask(0x0Fu);
    memset((void *)VGA_MEMORY, 0x00, VGA_MEMORY_SIZE);
}

/**
 * Reverse the bit order of an 8-bit value.
 *
 * @param value Byte whose bits will be reversed.
 * @returns The input byte with its bit order reversed (bit 0 becomes bit 7, bit 1 becomes bit 6, etc.).
 */
static inline uint8_t bit_reverse(uint8_t value)
{
    value = (uint8_t)(((value & 0x55u) << 1) | ((value & 0xAAu) >> 1));
    value = (uint8_t)(((value & 0x33u) << 2) | ((value & 0xCCu) >> 2));
    return (uint8_t)((value << 4) | (value >> 4));
}

/**
 * Retrieve the bitmask for a given scanline of an IBM VGA 8x16 glyph with bit order flipped to match the renderer.
 *
 * @param ch Glyph index (character code) into the IBM VGA 8x16 font table.
 * @param scanline Row within the glyph (0-based). Values >= CELL_HEIGHT produce a zero mask.
 * @returns A byte whose bits represent the pixels for the requested scanline with MSB/LSB order reversed; `0` if `scanline` is out of range.
 */
static uint8_t glyph_row_bits(uint8_t ch, size_t scanline)
{
    if (scanline >= CELL_HEIGHT) {
        return 0;
    }
    /* IBM VGA font bytes store pixels MSB->LSB, so flip to match our renderer. */
    return bit_reverse(font_ibm_vga_8x16[ch][scanline]);
}

/**
 * Flush an 8x16 cell from the software framebuffer into planar VGA memory.
 *
 * Copies an 8x16 pixel block starting at (base_x, base_y) from the in-memory
 * framebuffer into the VGA bitmap (4 bitplanes), applying per-plane map
 * masks and restoring the full map mask on completion. The write is clipped
 * to the bottom of the screen if the cell extends past SCREEN_HEIGHT.
 *
 * @param base_x X coordinate in pixels of the cell's top-left corner (must be
 *               aligned to an 8-pixel column for correct plane packing).
 * @param base_y Y coordinate in pixels of the cell's top-left corner.
 */
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

/**
 * Render the character stored in the terminal cell at (row, col) into the framebuffer and commit it to VGA memory.
 *
 * Renders the cell's glyph using the cell's foreground and background colors into the in-memory framebuffer and then flushes the 8x16 cell area to the planar VGA framebuffer.
 *
 * @param row Row index of the cell to render; no operation if out of range.
 * @param col Column index of the cell to render; no operation if out of range.
 */
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

/**
 * Draw a block-style cursor overlay for the cell at the given row and column.
 *
 * Computes a cursor color from the cell's foreground/background and paints a
 * CURSOR_HEIGHT-high filled block over the bottom of the cell, then flushes
 * that cell to VGA memory.
 *
 * @param row Row index of the cell where the cursor block should be drawn.
 * @param col Column index of the cell where the cursor block should be drawn.
 *
 * If (row, col) is outside the valid TTY range the function returns without
 * making any changes.
 */
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

/**
 * Update the software cursor overlay to the current cursor position.
 *
 * Restores the character previously covered by the cursor (if it was on-screen),
 * updates the internal overlay coordinates to match the current cursor, then
 * redraws the character at the new position and overlays the block cursor.
 */
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

/**
 * Redraws every cell on the terminal and restores the software cursor overlay.
 *
 * Re-renders all character glyphs for every row and column, clears the stored
 * cursor-overlay markers, and then draws the cursor at the current cursor
 * position.
 */
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

/**
 * Scrolls the terminal content up when the cursor has moved past the last row.
 *
 * If the cursor is within the visible area this function does nothing. Otherwise
 * it advances the text buffer by the required number of rows (clamped to the
 * total row count), fills the newly exposed bottom rows with blank cells using
 * the current color, moves the cursor to the last row, and re-renders the screen.
 */
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

/**
 * Initialize the software VGA terminal and set the initial text color.
 *
 * Sets the active text color, resets the cursor and cursor overlay, clears the pixel framebuffer
 * and VGA display, populates the cell buffer with blank cells using the provided color, and
 * renders the initial screen.
 *
 * @param color Initial text color/attribute to use for subsequent output.
 */
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

/**
 * Set the active text color used for subsequent character output.
 *
 * @param color Color/attribute value to apply to future writes.
 */
void tty_set_color(uint8_t color)
{
    current_color = color;
}

/**
 * Handle a single input character and update terminal cells, cursor position, and the displayed framebuffer.
 *
 * Processes control characters and printable input:
 * - '\n': moves to the start of the next line, scrolls if needed, and updates the cursor overlay.
 * - '\r': moves to the start of the current line and updates the cursor overlay.
 * - '\b': performs a backspace by moving the cursor left (wrapping to the end of the previous line when necessary), clears the cell at the new cursor position (sets it to space with the current color), redraws that glyph, and updates the cursor overlay.
 * - Any other character: writes the character and current color into the current cell, renders the glyph, advances the cursor (wrapping to the next line when at end of column), scrolls if the cursor moved past the last row, and updates the cursor overlay.
 *
 * Side effects: modifies the global cell buffer, framebuffer/VGA memory via glyph rendering and flushing, and global cursor_row/cursor_col state.
 */
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

/**
 * Write a buffer of characters to the TTY, processing control characters and advancing the cursor as each byte is written.
 *
 * @param data Pointer to the buffer containing characters to write.
 * @param len Number of bytes from `data` to process and write to the terminal.
 */
void tty_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        tty_putc(data[i]);
    }
}

/**
 * Write a null-terminated string to the terminal starting at the current cursor position.
 *
 * The string is written character-by-character using the active text color; control characters
 * such as '\n', '\r', and '\b' are handled by the terminal's normal semantics (line feed,
 * carriage return, backspace). The cursor and display are updated as characters are emitted.
 *
 * @param str Null-terminated C string to write. Must not be NULL.
 */
void tty_write_string(const char *str)
{
    tty_write(str, strlen(str));
}

/**
 * Clear the terminal buffer, reset the cursor to the top-left cell, and redraw the display.
 *
 * All cells are set to a space character using the current text color, the cursor_row and
 * cursor_col are set to 0, and the entire screen is re-rendered to reflect the cleared state.
 */
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

/**
 * Retrieve the number of text rows in the terminal.
 *
 * @returns The number of text rows available (TTY_ROWS).
 */
size_t tty_rows(void)
{
    return TTY_ROWS;
}

/**
 * Get the number of text columns in the terminal.
 *
 * @returns The number of columns available for text output.
 */
size_t tty_cols(void)
{
    return TTY_COLS;
}

/**
 * Write a character and its color attribute into a specific terminal cell and update the display.
 *
 * Updates the in-memory cell at (row, col) with the supplied character and 8-bit color attribute,
 * renders the glyph for that cell to the framebuffer/VGA, and refreshes the software cursor overlay
 * if the written cell is the current cursor position.
 *
 * @param row Row index of the target cell (0-based).
 * @param col Column index of the target cell (0-based).
 * @param c Character to store in the cell.
 * @param color 8-bit color attribute for the cell (foreground/background encoded).
 */
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

/**
 * Retrieve the character stored at the specified terminal cell.
 *
 * @param row Row index of the cell (0-based).
 * @param col Column index of the cell (0-based).
 * @returns The character contained in the cell, or `'\0'` if `row` or `col` is outside the valid range.
 */
char tty_get_cell_character(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return '\0';
    }

    return cells[tty_cell_index(row, col)].character;
}

/**
 * Retrieve the color attribute of the cell at the given row and column.
 *
 * @param row Zero-based row index of the cell.
 * @param col Zero-based column index of the cell.
 * @returns The cell's color value; `0` if `row` or `col` is out of range.
 */
uint8_t tty_get_cell_color(size_t row, size_t col)
{
    if (row >= TTY_ROWS || col >= TTY_COLS) {
        return 0;
    }

    return cells[tty_cell_index(row, col)].color;
}

/**
 * Set the terminal cursor to a specific cell and update the on-screen cursor.
 *
 * The provided row and column are treated as zero-based indices and will be
 * clamped to the valid range [0, tty_rows()-1] and [0, tty_cols()-1] if out
 * of bounds. After updating the internal cursor position, the visible cursor
 * overlay is refreshed.
 *
 * @param row Desired cursor row (zero-based); values outside the valid range
 *            are clamped to the nearest valid row.
 * @param col Desired cursor column (zero-based); values outside the valid
 *            range are clamped to the nearest valid column.
 */
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

/**
 * Retrieve the current terminal cursor position.
 * @param row Pointer to receive the cursor row index; if NULL the row is not written.
 * @param col Pointer to receive the cursor column index; if NULL the column is not written.
 */
void tty_get_cursor_position(size_t *row, size_t *col)
{
    if (row) {
        *row = cursor_row;
    }
    if (col) {
        *col = cursor_col;
    }
}
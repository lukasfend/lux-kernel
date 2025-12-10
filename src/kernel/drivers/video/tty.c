/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: VGA text-mode terminal implementation with scrolling and cursor management.
 */
#include <lux/io.h>
#include <string.h>
#include <lux/tty.h>

static volatile uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static size_t cursor_row = 0;
static size_t cursor_col = 0;
static uint8_t current_color = 0x07;

static uint16_t make_entry(char c)
{
    return (uint16_t)current_color << 8 | (uint8_t)c;
}

/**
 * Scrolls the text buffer up by one line when the cursor is beyond the bottom of the screen.
 *
 * If the cursor is within the visible region, no action is taken. When scrolling is required,
 * the visible contents move up one row, the last row is cleared to space characters using the
 * current text color attribute, and `cursor_row` is placed on the last visible row.
 */
static void tty_scroll(void)
{
    if (cursor_row < VGA_HEIGHT)
    {
        return;
    }

    const size_t row_bytes = VGA_WIDTH * sizeof(uint16_t);
    const size_t visible_rows = VGA_HEIGHT - 1;
    memmove((void *)VGA_MEMORY,
            (const void *)(VGA_MEMORY + VGA_WIDTH),
            row_bytes * visible_rows);

    uint16_t blank = make_entry(' ');
    for (size_t col = 0; col < VGA_WIDTH; ++col)
    {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;
    }

    cursor_row = VGA_HEIGHT - 1;
}

/**
 * Synchronize the VGA text-mode hardware cursor with the current internal cursor position.
 *
 * Clamps internal row/column to the visible screen bounds before updating the hardware
 * cursor so the displayed cursor always lies within [0, VGA_HEIGHT-1] x [0, VGA_WIDTH-1].
 */
static void tty_sync_cursor(void)
{
    size_t row = cursor_row;
    size_t col = cursor_col;
    if (row >= VGA_HEIGHT)
    {
        row = VGA_HEIGHT - 1;
    }
    if (col >= VGA_WIDTH)
    {
        col = VGA_WIDTH - 1;
    }

    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}

/**
 * Initialize the TTY driver state and clear the display.
 *
 * Sets the active text attribute to `color`, resets the cursor position to the
 * top-left corner, fills the entire VGA text buffer with spaces using the
 * chosen attribute, and updates the hardware cursor to the new position.
 *
 * @param color VGA text attribute byte to use for subsequent output.
 */
void tty_init(uint8_t color)
{
    current_color = color;
    cursor_row = 0;
    cursor_col = 0;

    uint16_t blank = make_entry(' ');
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i)
    {
        VGA_MEMORY[i] = blank;
    }

    tty_sync_cursor();
}

void tty_set_color(uint8_t color)
{
    current_color = color;
}

/**
 * Write a single character to the VGA text console at the current cursor position.
 *
 * Handles control characters specially: `'\n'` moves the cursor to the start of the next line and triggers scrolling if needed; `'\r'` moves the cursor to the start of the current line; `'\b'` (backspace) moves the cursor left (wrapping to the previous line when at column 0) and clears the cell. For all other characters, the character is written at the cursor and the cursor is advanced, wrapping to the next line and triggering scroll when necessary. The hardware cursor is synchronized after any change.
 */
void tty_putc(char c)
{
    if (c == '\n')
    {
        cursor_col = 0;
        ++cursor_row;
        tty_scroll();
        tty_sync_cursor();
        return;
    }

    if (c == '\r')
    {
        cursor_col = 0;
        tty_sync_cursor();
        return;
    }

    if (c == '\b')
    {
        if (cursor_col > 0)
        {
            --cursor_col;
        }
        else if (cursor_row > 0)
        {
            --cursor_row;
            cursor_col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = make_entry(' ');
        tty_sync_cursor();
        return;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = make_entry(c);
    if (++cursor_col >= VGA_WIDTH)
    {
        cursor_col = 0;
        ++cursor_row;
    }
    tty_scroll();
    tty_sync_cursor();
}

/**
 * Write a sequence of characters to the terminal.
 *
 * Writes `len` bytes from `data` to the active text console. Control characters
 * such as newline (`'\n'`), carriage return (`'\r'`), and backspace (`'\b'`)
 * are interpreted and affect cursor position and scrolling.
 *
 * @param data Pointer to the buffer containing characters to write.
 * @param len  Number of characters to write from `data`.
 */
void tty_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        tty_putc(data[i]);
    }
}

/**
 * Write a null-terminated string to the terminal using the current text color.
 *
 * @param str Null-terminated C string whose characters will be written starting
 *            at the current cursor position; the cursor will advance and the
 *            display will scroll as needed.
 */
void tty_write_string(const char *str)
{
    tty_write(str, strlen(str));
}

/**
 * Clear the text console and reset the cursor to the top-left corner.
 *
 * Fills the entire VGA text buffer with space characters using the current
 * color attribute, sets the internal cursor position to row 0, column 0,
 * and updates the hardware text-mode cursor to match.
 */
void tty_clear(void)
{

    cursor_row = 0;
    cursor_col = 0;
    uint16_t blank = make_entry(' ');
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i)
    {
        VGA_MEMORY[i] = blank;
    }
    tty_sync_cursor();
}

/**
 * Get the number of text-mode rows on the VGA screen.
 * @returns The number of text-mode rows (screen height).
 */
size_t tty_rows(void)
{
    return VGA_HEIGHT;
}

/**
 * Get the number of columns in the text-mode screen.
 *
 * @returns Number of columns (width) of the VGA text-mode display.
 */
size_t tty_cols(void)
{
    return VGA_WIDTH;
}

/**
 * Write a character with a color attribute to a specific screen cell.
 *
 * Writes `c` with the VGA text-mode attribute `color` into the video buffer at
 * the given zero-based `row` and `col`. If `row` or `col` is outside the
 * visible screen bounds, the function returns without modifying video memory.
 *
 * @param row Zero-based row index (0..VGA_HEIGHT-1).
 * @param col Zero-based column index (0..VGA_WIDTH-1).
 * @param c Character to write at the specified cell.
 * @param color VGA text-mode attribute byte (foreground/background and flags).
 */
void tty_write_cell(size_t row, size_t col, char c, uint8_t color)
{
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH)
    {
        return;
    }

    VGA_MEMORY[row * VGA_WIDTH + col] = (uint16_t) color << 8 | (uint8_t) c;
}
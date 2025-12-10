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

static void tty_scroll(void)
{
    if (cursor_row < VGA_HEIGHT) {
        return;
    }

    const size_t row_bytes = VGA_WIDTH * sizeof(uint16_t);
    const size_t visible_rows = VGA_HEIGHT - 1;
    memmove((void *)VGA_MEMORY,
            (const void *)(VGA_MEMORY + VGA_WIDTH),
            row_bytes * visible_rows);

    uint16_t blank = make_entry(' ');
    for (size_t col = 0; col < VGA_WIDTH; ++col) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;
    }

    cursor_row = VGA_HEIGHT - 1;
}

static void tty_sync_cursor(void)
{
    size_t row = cursor_row;
    size_t col = cursor_col;
    if (row >= VGA_HEIGHT) {
        row = VGA_HEIGHT - 1;
    }
    if (col >= VGA_WIDTH) {
        col = VGA_WIDTH - 1;
    }

    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}

void tty_init(uint8_t color)
{
    current_color = color;
    cursor_row = 0;
    cursor_col = 0;

    uint16_t blank = make_entry(' ');
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        VGA_MEMORY[i] = blank;
    }

    tty_sync_cursor();
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
        tty_sync_cursor();
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        tty_sync_cursor();
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            --cursor_col;
        } else if (cursor_row > 0) {
            --cursor_row;
            cursor_col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = make_entry(' ');
        tty_sync_cursor();
        return;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = make_entry(c);
    if (++cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        ++cursor_row;
    }
    tty_scroll();
    tty_sync_cursor();
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

void tty_clear(void) {
    cursor_row = 0;
    cursor_col = 0;
    uint16_t blank = make_entry(' ');
    for(size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        VGA_MEMORY[i] = blank;
    }
    tty_sync_cursor();
}
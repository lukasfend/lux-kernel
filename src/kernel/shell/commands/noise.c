/*
 * Date: 2025-12-10 16:12 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: A clear function to clear the terminal buffer
 */

#include <lux/shell.h>
#include <lux/tty.h>

#define NOISE_FRAMES 300U
#define NOISE_DELAY_ITER 200000U

static uint32_t noise_rand(void) {
    static uint32_t state = 0xC0FFEE01U;
    state = 1664525U * state + 1013904223U;
    return state;
}

static void noise_delay(void) {
    for(volatile uint32_t i = 0; i < NOISE_DELAY_ITER; ++i) {
        __asm__ volatile("pause");
    }
}

static void draw_noise_frame(void)
{
    static const uint8_t palette[] = {
        (0x0u << 4) | 0xF, /* white on black */
        (0x0u << 4) | 0x7, /* light gray on black */
        (0x8u << 4) | 0xF, /* white on dark gray */
        (0x7u << 4) | 0x0, /* black on light gray */
        (0x8u << 4) | 0x7, /* light gray on dark gray */
    };
    static const char glyphs[] = { '#', ' ' };

    const size_t rows = tty_rows();
    const size_t cols = tty_cols();
    const size_t palette_len = sizeof(palette) / sizeof(palette[0]);
    const size_t glyph_count = sizeof(glyphs) / sizeof(glyphs[0]);

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            uint8_t attr = palette[noise_rand() % palette_len];
            char glyph = glyphs[noise_rand() % glyph_count];
            tty_write_cell(row, col, glyph, attr);
        }
    }
}

static void noise_handler(int argc, char **argv) {
    (void)argc;
    (void)argv;
    tty_clear();

    for(uint32_t frame = 0; frame < NOISE_FRAMES; ++frame) {
        draw_noise_frame();
        noise_delay(); 
    }
    tty_clear();
    tty_write_string("Noise done.\n");
}

const struct shell_command shell_command_noise = {
    .name = "noise",
    .help = "Generates random noise for a few seconds",
    .handler = noise_handler
}; 
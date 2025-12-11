/*
 * Date: 2025-12-10 16:12 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: A clear function to clear the terminal buffer
 */

#include <lux/shell.h>
#include <lux/time.h>
#include <lux/tty.h>

#define NOISE_FRAMES 300U
#define NOISE_FRAME_DELAY_MS 25U

/**
 * Advance the internal pseudo-random generator and return the next value.
 *
 * The generator maintains internal state across calls; each invocation updates
 * that state and yields a new 32-bit pseudo-random value.
 *
 * @returns The next 32-bit pseudo-random value from the generator.
 */
static uint32_t noise_rand(void) {
    static uint32_t state = 0xC0FFEE01U;
    state = 1664525U * state + 1013904223U;
    return state;
}

/**
 * Pause for the configured frame interval but return early if a shell interrupt is requested.
 *
 * @returns `true` if the full frame delay completed without an interrupt, `false` otherwise.
 */
static bool noise_delay(void) {
    for (uint32_t i = 0; i < NOISE_FRAME_DELAY_MS; ++i) {
        if (shell_command_should_stop()) {
            return false;
        }
        sleep_ms(1);
    }
    return true;
}

/**
 * Render one frame of visual noise to the terminal by filling every cell with a randomly
 * selected glyph and text attribute.
 *
 * The function writes to each terminal row and column so that the entire visible screen
 * is updated with the noise frame.
 */
static void draw_noise_frame(void)
{
    static const uint8_t palette[] = {
        (0x0u << 4) | 0xA, /* light green on black */
        (0x0u << 4) | 0x2, /* green on black */
        (0x2u << 4) | 0xA, /* light green on green */
        (0xAu << 4) | 0x0, /* black on light green */
        (0x2u << 4) | 0x0, /* black on green */
    };
    static const char glyphs[] = { '1','2','3','4','5','6','7','8','9' };

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

/**
 * Render animated random noise in the terminal for a fixed duration.
 *
 * Clears the terminal, draws up to NOISE_FRAMES noise frames with interrupt-aware
 * delays between frames, and clears the terminal on exit. If not interrupted,
 * writes "Noise done.\n" to the provided shell I/O.
 *
 * @param io Shell I/O used to write the completion message.
 */
static void noise_handler(int argc, char **argv, const struct shell_io *io) {
    (void)argc;
    (void)argv;
    tty_clear();

    for(uint32_t frame = 0; frame < NOISE_FRAMES; ++frame) {
        if (shell_command_should_stop()) {
            tty_clear();
            return;
        }

        draw_noise_frame();

        if (!noise_delay()) {
            tty_clear();
            return;
        }
    }
    tty_clear();
    shell_io_write_string(io, "Noise done.\n");
}

const struct shell_command shell_command_noise = {
    .name = "noise",
    .help = "Generates random noise for a few seconds",
    .handler = noise_handler
}; 
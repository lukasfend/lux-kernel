#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>
#include <lux/time.h> 

/**
 * Parse a NUL-terminated decimal string into a 32-bit unsigned integer.
 *
 * Attempts to convert `text` (ASCII digits only) into a uint32_t and store the
 * result in `*value`. Fails if `text` is NULL or empty, contains any
 * non-digit characters, or represents a value greater than 0xFFFFFFFF.
 *
 * @param text NUL-terminated ASCII string containing decimal digits to parse.
 * @param value Output pointer that receives the parsed uint32_t on success.
 * @returns `true` if parsing succeeded and `*value` was set, `false` otherwise.
 */
static bool parse_u32(const char *text, uint32_t *value)
{
    if (!text || !*text || !value) {
        return false;
    }

    uint32_t result = 0;
    for (size_t i = 0; text[i]; ++i) {
        char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }

        uint32_t digit = (uint32_t)(c - '0');
        const uint32_t max_value = 0xFFFFFFFFu;
        if (result > (max_value - digit) / 10u) {
            return false;
        }
        result = result * 10u + digit;
    }

    *value = result;
    return true;
}

/**
 * Waits for the given number of milliseconds, but returns early if a shell interrupt is detected.
 *
 * @param duration Duration to wait in milliseconds.
 * @returns `true` if the full duration elapsed without interruption, `false` if interrupted. 
 */
static bool sleep_interruptible(uint32_t duration)
{
    for (uint32_t elapsed = 0; elapsed < duration; ++elapsed) {
        if (shell_command_should_stop()) {
            return false;
        }
        sleep_ms(1);
    }

    return true;
}

/**
 * Handle the shell "sleep" command.
 *
 * Parses a single argument as milliseconds, writes a usage message or an invalid-value
 * error to the provided shell IO on failure, and pauses execution for the specified
 * duration with interruptible behavior on success.
 *
 * @param argc Number of arguments passed to the command (expected 2: command and milliseconds).
 * @param argv Argument vector; argv[1] is parsed as the millisecond duration.
 * @param io    Shell IO interface used for writing usage and error messages.
 */
static void sleep_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc != 2) {
        shell_io_write_string(io, "Usage: sleep <milliseconds>\n");
        return;
    }

    uint32_t duration = 0;
    if (!parse_u32(argv[1], &duration)) {
        shell_io_write_string(io, "sleep: invalid millisecond value\n");
        return;
    }

    sleep_interruptible(duration);
}

const struct shell_command shell_command_sleep = {
    .name = "sleep",
    .help = "Pause execution for N milliseconds",
    .handler = sleep_handler,
};
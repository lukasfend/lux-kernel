#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>
#include <lux/time.h> 

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

    sleep_ms(duration);
}

const struct shell_command shell_command_sleep = {
    .name = "sleep",
    .help = "Pause execution for N milliseconds",
    .handler = sleep_handler,
};

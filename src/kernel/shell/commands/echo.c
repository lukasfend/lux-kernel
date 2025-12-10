#include <lux/shell.h>

/**
 * Echo the provided command-line arguments to the given shell IO as a single line.
 *
 * Writes argv[1] through argv[argc-1] separated by single spaces, then appends a newline.
 *
 * @param argc Number of elements in argv.
 * @param argv Array of argument strings; argv[0] (the command name) is ignored.
 * @param io Output context used for writing strings and characters.
 */
static void echo_handler(int argc, char **argv, const struct shell_io *io)
{
    for (int i = 1; i < argc; ++i) {
        shell_io_write_string(io, argv[i]);
        if (i + 1 < argc) {
            shell_io_putc(io, ' ');
        }
    }
    shell_io_putc(io, '\n');
}

const struct shell_command shell_command_echo = {
    .name = "echo",
    .help = "Echo the provided text",
    .handler = echo_handler,
};
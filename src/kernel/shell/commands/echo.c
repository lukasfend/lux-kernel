#include <lux/shell.h>
#include <lux/tty.h>

/**
 * Print provided command-line arguments to the TTY as a single line.
 *
 * Writes argv[1] through argv[argc-1] to the TTY separated by single spaces,
 * then terminates the line with a newline character.
 *
 * @param argc Number of elements in argv.
 * @param argv Array of argument strings; argv[0] (the command name) is ignored.
 */
static void echo_handler(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        tty_write_string(argv[i]);
        if (i + 1 < argc) {
            tty_putc(' ');
        }
    }
    tty_putc('\n');
}

const struct shell_command shell_command_echo = {
    .name = "echo",
    .help = "Echo the provided text",
    .handler = echo_handler,
};
/*
 * Date: 2025-12-10 16:12 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: A clear function to clear the terminal buffer
 */

#include <lux/shell.h>
#include <lux/tty.h>

/**
 * Clear the terminal buffer for the "clear" shell command.
 *
 * This command ignores any provided arguments and I/O context.
 *
 * @param argc Number of arguments (ignored).
 * @param argv Argument array (ignored).
 * @param io Shell I/O interface (ignored).
 */
static void clear_handler(int argc, char **argv, const struct shell_io *io) {
    (void)argc;
    (void)argv;
    (void)io;
    tty_clear();
}

const struct shell_command shell_command_clear = {
    .name = "clear",
    .help = "Clears the current terminal buffer",
    .handler = clear_handler
}; 
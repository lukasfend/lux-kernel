/*
 * Date: 2025-12-10 16:12 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: A clear function to clear the terminal buffer
 */

#include <lux/shell.h>
#include <lux/tty.h>

/**
 * Handle the "clear" shell command by clearing the terminal buffer.
 *
 * This command ignores any arguments.
 *
 * @param argc Number of arguments (ignored).
 * @param argv Argument array (ignored).
 */
static void clear_handler(int argc, char **argv) {
    tty_clear();
}

const struct shell_command shell_command_clear = {
    .name = "clear",
    .help = "Clears the current terminal buffer",
    .handler = clear_handler
}; 
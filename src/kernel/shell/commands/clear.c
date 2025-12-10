/*
 * Date: 2025-12-10 16:12 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: A clear function to clear the terminal buffer
 */

#include <lux/shell.h>
#include <lux/tty.h>

static void clear_handler(int argc, char **argv) {
    tty_clear();
}

const struct shell_command shell_command_clear = {
    .name = "clear",
    .help = "Clears the current terminal buffer",
    .handler = clear_handler
}; 
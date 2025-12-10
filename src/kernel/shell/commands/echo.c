#include <lux/shell.h>
#include <lux/tty.h>

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

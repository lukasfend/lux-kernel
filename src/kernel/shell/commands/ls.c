#include <lux/shell.h>
#include <lux/tty.h>

static void ls_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tty_write_string("Filesystem support is not available yet.\n");
    tty_write_string("Stub 'ls' command: implement storage drivers to list directories.\n");
}

const struct shell_command shell_command_ls = {
    .name = "ls",
    .help = "List directory contents (stub)",
    .handler = ls_handler,
};

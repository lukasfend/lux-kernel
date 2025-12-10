#include <lux/shell.h>

static void ls_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    shell_io_write_string(io, "Filesystem support is not available yet.\n");
    shell_io_write_string(io, "Stub 'ls' command: implement storage drivers to list directories.\n");
}

const struct shell_command shell_command_ls = {
    .name = "ls",
    .help = "List directory contents (stub)",
    .handler = ls_handler,
};

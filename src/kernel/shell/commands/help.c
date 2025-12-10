#include <lux/shell.h>

/**
 * Print a formatted list of built-in shell commands to the TTY.
 *
 * Obtains the list of registered built-in commands and writes a header
 * "Available commands:" followed by each command's name and help text on
 * its own line.
 *
 * @param argc Unused command argument count.
 * @param argv Unused command argument vector.
 */
static void help_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    size_t count = 0;
    const struct shell_command *const *commands = shell_builtin_commands(&count);

    shell_io_write_string(io, "Available commands:\n");
    for (size_t i = 0; i < count; ++i) {
        shell_io_write_string(io, "  ");
        shell_io_write_string(io, commands[i]->name);
        shell_io_write_string(io, " - ");
        shell_io_write_string(io, commands[i]->help);
        shell_io_putc(io, '\n');
    }
}

const struct shell_command shell_command_help = {
    .name = "help",
    .help = "List available commands",
    .handler = help_handler,
};
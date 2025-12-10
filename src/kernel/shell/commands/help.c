#include <lux/shell.h>

/**
 * Display the list of built-in shell commands and their help text using the provided IO.
 *
 * Writes a header "Available commands:" followed by each command name and its help
 * text on a separate line to the given shell IO implementation.
 *
 * @param argc Ignored.
 * @param argv Ignored.
 * @param io IO interface used to emit the header and command entries.
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
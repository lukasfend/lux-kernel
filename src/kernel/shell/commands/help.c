#include <lux/shell.h>
#include <lux/tty.h>

static void help_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    size_t count = 0;
    const struct shell_command *const *commands = shell_builtin_commands(&count);

    tty_write_string("Available commands:\n");
    for (size_t i = 0; i < count; ++i) {
        tty_write_string("  ");
        tty_write_string(commands[i]->name);
        tty_write_string(" - ");
        tty_write_string(commands[i]->help);
        tty_putc('\n');
    }
}

const struct shell_command shell_command_help = {
    .name = "help",
    .help = "List available commands",
    .handler = help_handler,
};

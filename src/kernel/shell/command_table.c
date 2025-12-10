#include <lux/shell.h>

extern const struct shell_command shell_command_help;
extern const struct shell_command shell_command_echo;
extern const struct shell_command shell_command_shutdown;

const struct shell_command *const *shell_builtin_commands(size_t *count)
{
    static const struct shell_command *const builtin[] = {
        &shell_command_help,
        &shell_command_echo,
        &shell_command_shutdown,
    };

    if (count) {
        *count = sizeof(builtin) / sizeof(builtin[0]);
    }

    return builtin;
}

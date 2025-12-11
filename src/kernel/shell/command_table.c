#include <lux/shell.h>

extern const struct shell_command shell_command_help;
extern const struct shell_command shell_command_cd;
extern const struct shell_command shell_command_echo;
extern const struct shell_command shell_command_shutdown;
extern const struct shell_command shell_command_clear;
extern const struct shell_command shell_command_noise;
extern const struct shell_command shell_command_meminfo;
extern const struct shell_command shell_command_hexdump;
extern const struct shell_command shell_command_ls;
extern const struct shell_command shell_command_cat;
extern const struct shell_command shell_command_less;
extern const struct shell_command shell_command_touch;
extern const struct shell_command shell_command_sleep;
extern const struct shell_command shell_command_printf;
extern const struct shell_command shell_command_mkdir;

/**
 * Provide the table of built-in shell commands.
 *
 * @param count Optional output. If non-NULL, set to the number of entries in the returned table.
 * @returns Pointer to a static array of pointers to the built-in `struct shell_command`; the array and its elements remain valid for the program lifetime.
 */
const struct shell_command *const *shell_builtin_commands(size_t *count)
{
    static const struct shell_command *const builtin[] = {
        &shell_command_help,
        &shell_command_cd,
        &shell_command_echo,
        &shell_command_shutdown,
        &shell_command_clear,
        &shell_command_noise,
        &shell_command_meminfo,
        &shell_command_hexdump,
        &shell_command_ls,
        &shell_command_cat,
        &shell_command_less,
        &shell_command_touch,
        &shell_command_mkdir,
        &shell_command_sleep,
        &shell_command_printf
    };

    if (count) {
        *count = sizeof(builtin) / sizeof(builtin[0]);
    }

    return builtin;
}
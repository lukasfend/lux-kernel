/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal interactive shell handling input, parsing, and built-ins.
 */
#include <lux/keyboard.h>
#include <lux/shell.h>
#include <string.h>
#include <lux/tty.h>

#define INPUT_BUFFER_SIZE 128
#define MAX_ARGS 8

/**
 * Locate a shell command by name in a supplied array of command pointers.
 *
 * @param name Null-terminated command name to search for.
 * @param commands Array of pointers to `struct shell_command` to search.
 * @param command_count Number of elements in `commands`.
 * @return Pointer to the matching `struct shell_command`, or NULL if no match is found.
 */
static const struct shell_command *find_command(const char *name, const struct shell_command *const *commands, size_t command_count)
{
    for (size_t i = 0; i < command_count; ++i) {
        if (strcmp(commands[i]->name, name) == 0) {
            return commands[i];
        }
    }
    return 0;
}

/**
 * Write the shell prompt "lux> " to the TTY.
 */
static void prompt(void)
{
    tty_write_string("lux> ");
}

static size_t read_line(char *buffer, size_t capacity)
{
    size_t len = 0;

    while (len + 1 < capacity) {
        char c = keyboard_read_char();
        if (!c) {
            continue;
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            tty_putc('\n');
            break;
        }

        if (c == '\b') {
            if (len) {
                --len;
                tty_putc('\b');
            }
            continue;
        }

        buffer[len++] = c;
        tty_putc(c);
    }

    buffer[len] = '\0';
    return len;
}

/**
 * Split a mutable input line into space-separated tokens and store pointers to them in argv up to max_args.
 * @param line Modifiable null-terminated string containing the input; spaces are replaced with `'\0'` to terminate tokens.
 * @param argv Array to receive pointers to the start of each token.
 * @param max_args Maximum number of tokens to store in `argv`.
 * @returns The number of tokens parsed and stored in `argv`.
 */
static int tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *cursor = line;

    while (*cursor && argc < max_args) {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (!*cursor) {
            break;
        }

        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor == ' ') {
            *cursor++ = '\0';
        }
    }

    return argc;
}

/**
 * Run the interactive shell loop.
 *
 * Initializes builtin commands and, if available, enters a read-evaluate loop that:
 * displays a prompt, reads a line from the keyboard, tokenizes it into arguments,
 * looks up a matching command by name, and invokes the command's handler.
 *
 * If no builtin commands are registered, writes an error message and returns without
 * entering the interactive loop. All user interaction is performed via the TTY.
 */
void shell_run(void)
{
    char buffer[INPUT_BUFFER_SIZE];
    char *argv[MAX_ARGS];
    size_t command_count = 0;
    const struct shell_command *const *commands = shell_builtin_commands(&command_count);

    if (!commands || !command_count) {
        tty_write_string("Unable to start shell: no commands registered.\n");
        return;
    }

    tty_write_string("Type 'help' for a list of commands.\n");

    for (;;) {
        prompt();
        size_t len = read_line(buffer, sizeof(buffer));
        if (!len) {
            continue;
        }

        int argc = tokenize(buffer, argv, MAX_ARGS);
        if (!argc) {
            continue;
        }

        const struct shell_command *cmd = find_command(argv[0], commands, command_count);
        if (!cmd) {
            tty_write_string("Unknown command: ");
            tty_write_string(argv[0]);
            tty_putc('\n');
            continue;
        }

        cmd->handler(argc, argv);
    }
}
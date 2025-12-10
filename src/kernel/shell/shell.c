/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal interactive shell handling input, parsing, and built-ins.
 */
#include <lux/keyboard.h>
#include <lux/shell.h>
#include <stdbool.h>
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

static size_t common_prefix_len(const char *a, const char *b)
{
    size_t len = 0;
    while (a[len] && b[len] && a[len] == b[len]) {
        ++len;
    }
    return len;
}

static bool command_matches_prefix(const char *name, const char *prefix, size_t prefix_len)
{
    if (!prefix_len) {
        return true;
    }

    for (size_t i = 0; i < prefix_len; ++i) {
        if (!name[i] || name[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

static bool buffer_has_space(const char *buffer, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == ' ') {
            return true;
        }
    }
    return false;
}

static void redraw_prompt_with_buffer(const char *buffer, size_t len)
{
    prompt();
    for (size_t i = 0; i < len; ++i) {
        tty_putc(buffer[i]);
    }
}

static void list_matches(const char *buffer, size_t len, const struct shell_command *const *commands, size_t command_count)
{
    tty_putc('\n');
    for (size_t i = 0; i < command_count; ++i) {
        const char *name = commands[i]->name;
        if (command_matches_prefix(name, buffer, len)) {
            tty_write_string(name);
            tty_putc('\n');
        }
    }
    redraw_prompt_with_buffer(buffer, len);
}

static void handle_tab_completion(char *buffer, size_t *len, size_t capacity, const struct shell_command *const *commands, size_t command_count)
{
    if (!commands || !command_count) {
        return;
    }

    if (!*len) {
        list_matches(buffer, *len, commands, command_count);
        return;
    }

    if (buffer_has_space(buffer, *len)) {
        tty_putc('\a');
        return;
    }

    size_t prefix_len = *len;
    size_t match_count = 0;
    const char *first_name = 0;
    size_t shared_len = 0;

    for (size_t i = 0; i < command_count; ++i) {
        const char *name = commands[i]->name;
        if (!command_matches_prefix(name, buffer, prefix_len)) {
            continue;
        }

        if (!match_count) {
            first_name = name;
            shared_len = strlen(name);
        } else {
            size_t common = common_prefix_len(first_name, name);
            if (common < shared_len) {
                shared_len = common;
            }
        }
        ++match_count;
    }

    if (!match_count) {
        tty_putc('\a');
        return;
    }

    size_t appended = 0;
    if (shared_len > prefix_len) {
        size_t to_add = shared_len - prefix_len;
        size_t room = (capacity - 1u) - prefix_len;
        if (to_add > room) {
            to_add = room;
        }

        if (to_add && first_name) {
            memcpy(buffer + prefix_len, first_name + prefix_len, to_add);
            appended = to_add;
            *len += to_add;
            buffer[*len] = '\0';
            for (size_t i = 0; i < to_add; ++i) {
                tty_putc(buffer[prefix_len + i]);
            }
        }
    }

    if (match_count == 1 && first_name) {
        size_t name_len = strlen(first_name);
        if (*len == name_len && *len + 1 < capacity) {
            buffer[*len] = ' ';
            ++(*len);
            buffer[*len] = '\0';
            tty_putc(' ');
        }
        return;
    }

    if (match_count > 1 && appended == 0) {
        list_matches(buffer, *len, commands, command_count);
    }
}

static size_t read_line(char *buffer, size_t capacity, const struct shell_command *const *commands, size_t command_count)
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

        if (c == '\t') {
            handle_tab_completion(buffer, &len, capacity, commands, command_count);
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
        size_t len = read_line(buffer, sizeof(buffer), commands, command_count);
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
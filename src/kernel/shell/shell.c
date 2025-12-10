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
#define HISTORY_SIZE 16
#define MAX_PIPE_SEGMENTS 4
#define PIPE_BUFFER_CAPACITY 1024

static size_t shell_strnlen(const char *str, size_t max_len)
{
    size_t len = 0;
    if (!str) {
        return 0;
    }
    while (len < max_len && str[len]) {
        ++len;
    }
    return len;
}

struct shell_pipe_buffer {
    char data[PIPE_BUFFER_CAPACITY];
    size_t length;
    bool overflowed;
};

static char history[HISTORY_SIZE][INPUT_BUFFER_SIZE];
static size_t history_count;
static size_t history_head;

static void redraw_prompt_with_buffer(const char *buffer, size_t len);

static void pipe_buffer_init(struct shell_pipe_buffer *buffer)
{
    buffer->length = 0;
    buffer->overflowed = false;
    buffer->data[0] = '\0';
}

static void pipe_buffer_writer(void *context, const char *data, size_t len)
{
    struct shell_pipe_buffer *buffer = (struct shell_pipe_buffer *)context;
    if (!buffer || !data || !len) {
        return;
    }

    size_t remaining = (PIPE_BUFFER_CAPACITY - 1u) - buffer->length;
    if (!remaining) {
        buffer->overflowed = true;
        return;
    }

    if (len > remaining) {
        len = remaining;
        buffer->overflowed = true;
    }

    memcpy(buffer->data + buffer->length, data, len);
    buffer->length += len;
    buffer->data[buffer->length] = '\0';
}

static void tty_writer(void *context, const char *data, size_t len)
{
    (void)context;
    if (!data || !len) {
        return;
    }
    tty_write(data, len);
}

void shell_io_write(const struct shell_io *io, const char *data, size_t len)
{
    if (!io || !io->write || !data || !len) {
        return;
    }
    io->write(io->context, data, len);
}

void shell_io_putc(const struct shell_io *io, char c)
{
    shell_io_write(io, &c, 1u);
}

void shell_io_write_string(const struct shell_io *io, const char *str)
{
    if (!str) {
        return;
    }
    shell_io_write(io, str, strlen(str));
}

static void history_add(const char *line)
{
    if (!line || !*line) {
        return;
    }

    size_t len = shell_strnlen(line, INPUT_BUFFER_SIZE - 1u);
    if (!len) {
        return;
    }

    bool has_content = false;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] != ' ' && line[i] != '\t') {
            has_content = true;
            break;
        }
    }

    if (!has_content) {
        return;
    }

    size_t slot = history_head;
    memcpy(history[slot], line, len);
    history[slot][len] = '\0';

    history_head = (history_head + 1u) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        ++history_count;
    }
}

static const char *history_get(size_t offset)
{
    if (offset >= history_count) {
        return 0;
    }
    size_t index = (history_head + HISTORY_SIZE - 1u - offset) % HISTORY_SIZE;
    return history[index];
}

static void replace_buffer_with_text(char *buffer, size_t capacity, size_t *len, const char *text, size_t previous_len)
{
    size_t copy_len = 0;
    if (text) {
        copy_len = shell_strnlen(text, capacity - 1u);
        memcpy(buffer, text, copy_len);
    }
    buffer[copy_len] = '\0';
    *len = copy_len;

    tty_putc('\r');
    redraw_prompt_with_buffer(buffer, *len);

    if (previous_len > *len) {
        size_t diff = previous_len - *len;
        for (size_t i = 0; i < diff; ++i) {
            tty_putc(' ');
        }
        for (size_t i = 0; i < diff; ++i) {
            tty_putc('\b');
        }
    }
}

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
    int history_offset = -1;
    bool saved_current_valid = false;
    char saved_current[INPUT_BUFFER_SIZE];

    while (len + 1 < capacity) {
        char c = keyboard_read_char();
        if (!c) {
            continue;
        }

        if ((unsigned char)c == (unsigned char)KEYBOARD_KEY_ARROW_UP) {
            if ((size_t)(history_offset + 1) >= history_count) {
                tty_putc('\a');
                continue;
            }

            if (history_offset == -1 && !saved_current_valid) {
                size_t copy_len = len;
                size_t limit = capacity - 1u;
                if (copy_len > limit) {
                    copy_len = limit;
                }
                memcpy(saved_current, buffer, copy_len);
                saved_current[copy_len] = '\0';
                saved_current_valid = true;
            }

            ++history_offset;
            const char *entry = history_get((size_t)history_offset);
            if (!entry) {
                tty_putc('\a');
                --history_offset;
                continue;
            }

            size_t previous_len = len;
            replace_buffer_with_text(buffer, capacity, &len, entry, previous_len);
            continue;
        }

        if ((unsigned char)c == (unsigned char)KEYBOARD_KEY_ARROW_DOWN) {
            if (history_offset < 0) {
                tty_putc('\a');
                continue;
            }

            --history_offset;
            size_t previous_len = len;

            if (history_offset >= 0) {
                const char *entry = history_get((size_t)history_offset);
                if (!entry) {
                    history_offset = -1;
                } else {
                    replace_buffer_with_text(buffer, capacity, &len, entry, previous_len);
                    continue;
                }
            }

            const char *fallback = saved_current_valid ? saved_current : 0;
            replace_buffer_with_text(buffer, capacity, &len, fallback, previous_len);
            saved_current_valid = false;
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
            history_offset = -1;
            saved_current_valid = false;
            continue;
        }

        if (c == '\t') {
            history_offset = -1;
            saved_current_valid = false;
            handle_tab_completion(buffer, &len, capacity, commands, command_count);
            continue;
        }

        buffer[len++] = c;
        tty_putc(c);
        history_offset = -1;
        saved_current_valid = false;
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

static bool parse_pipeline(char *line, char **segments, size_t *segment_count)
{
    size_t count = 0;
    char *cursor = line;

    while (*cursor) {
        while (*cursor == ' ') {
            ++cursor;
        }

        if (!*cursor) {
            break;
        }

        if (count >= MAX_PIPE_SEGMENTS) {
            tty_write_string("Too many piped commands (max 4).\n");
            return false;
        }

        char *start = cursor;
        while (*cursor && *cursor != '|') {
            ++cursor;
        }

        char *end = cursor;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            --end;
        }

        if (end == start) {
            tty_write_string("Empty command in pipeline.\n");
            return false;
        }

        *end = '\0';
        segments[count++] = start;

        if (*cursor == '|') {
            *cursor = '\0';
            ++cursor;
            if (!*cursor) {
                tty_write_string("Trailing pipe requires another command.\n");
                return false;
            }
        }
    }

    if (!count) {
        return false;
    }

    *segment_count = count;
    return true;
}

static bool execute_pipeline(char **segments, size_t segment_count, const struct shell_command *const *commands, size_t command_count)
{
    char pipe_storage[PIPE_BUFFER_CAPACITY];
    size_t pipe_storage_len = 0;
    bool pipe_storage_valid = false;

    for (size_t i = 0; i < segment_count; ++i) {
        char *argv_local[MAX_ARGS];
        int argc = tokenize(segments[i], argv_local, MAX_ARGS);
        if (!argc) {
            tty_write_string("Empty command in pipeline.\n");
            return false;
        }

        const struct shell_command *cmd = find_command(argv_local[0], commands, command_count);
        if (!cmd) {
            tty_write_string("Unknown command: ");
            tty_write_string(argv_local[0]);
            tty_putc('\n');
            return false;
        }

        bool has_next = (i + 1u) < segment_count;
        struct shell_pipe_buffer pipe_buffer;
        struct shell_io io;

        if (pipe_storage_valid) {
            io.input = pipe_storage;
            io.input_len = pipe_storage_len;
        } else {
            io.input = 0;
            io.input_len = 0;
        }

        if (has_next) {
            pipe_buffer_init(&pipe_buffer);
            io.write = pipe_buffer_writer;
            io.context = &pipe_buffer;
        } else {
            io.write = tty_writer;
            io.context = 0;
        }

        cmd->handler(argc, argv_local, &io);

        if (has_next) {
            pipe_storage_len = pipe_buffer.length;
            memcpy(pipe_storage, pipe_buffer.data, pipe_storage_len + 1u);
            pipe_storage_valid = true;
            if (pipe_buffer.overflowed) {
                tty_write_string("\n[pipe] output truncated (buffer full)\n");
            }
        } else {
            pipe_storage_valid = false;
            pipe_storage_len = 0;
        }
    }

    return true;
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

        history_add(buffer);

        char *segments[MAX_PIPE_SEGMENTS];
        size_t segment_count = 0;
        if (!parse_pipeline(buffer, segments, &segment_count)) {
            continue;
        }

        execute_pipeline(segments, segment_count, commands, command_count);
    }
}
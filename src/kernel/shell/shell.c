/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal interactive shell handling input, parsing, and built-ins.
 */
#include <lux/interrupt.h>
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
#define SHELL_CTRL_C 0x03

/**
 * Compute the length of a C string up to a maximum limit.
 *
 * @param str Pointer to the NUL-terminated string to measure; may be NULL.
 * @param max_len Maximum number of characters to examine.
 * @return The number of bytes before the first NUL character, not exceeding `max_len`.
 *         Returns 0 if `str` is NULL or the first character is NUL.
 */
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

static char pending_input[INPUT_BUFFER_SIZE];
static size_t pending_input_head;
static size_t pending_input_tail;
static size_t pending_input_count;

static bool shell_interrupt_requested;
static bool shell_interrupt_announced;
static int shell_interrupt_subscription = -1;
static void shell_interrupt_reset_state(void);
static void shell_interrupt_handler(enum interrupt_signal signal, void *context);

static void redraw_prompt_with_buffer(const char *buffer, size_t len);

/**
 * Enqueue a character into the pending input circular buffer.
 *
 * If the buffer is full, the oldest pending character is discarded to make room.
 *
 * @param c Character to append to the pending input buffer.
 */
static void pending_input_push(char c)
{
    if (pending_input_count >= INPUT_BUFFER_SIZE) {
        pending_input_head = (pending_input_head + 1u) % INPUT_BUFFER_SIZE;
        --pending_input_count;
    }

    pending_input[pending_input_tail] = c;
    pending_input_tail = (pending_input_tail + 1u) % INPUT_BUFFER_SIZE;
    ++pending_input_count;
}

/**
 * Remove and return the next pending input character from the circular pending_input buffer.
 *
 * @param out Pointer to a char where the popped character will be stored; must not be NULL.
 * @returns `true` if a character was available and written to `out`, `false` if the buffer was empty or `out` is NULL.
 */
static bool pending_input_pop(char *out)
{
    if (!pending_input_count || !out) {
        return false;
    }

    *out = pending_input[pending_input_head];
    pending_input_head = (pending_input_head + 1u) % INPUT_BUFFER_SIZE;
    --pending_input_count;
    return true;
}

/**
 * Poll the keyboard for available scancodes and queue them for later processing.
 *
 * Reads all currently available characters from the keyboard and appends each
 * character that is not the shell's Ctrl-C sentinel to the pending input queue.
 * Ctrl-C scancodes are consumed here but not queued so the interrupt handler can
 * process them separately.
 */
static void collect_background_input(void)
{
    char symbol;
    while (keyboard_poll_char(&symbol)) {
        if ((unsigned char)symbol == (unsigned char)SHELL_CTRL_C) {
            continue;
        }
        pending_input_push(symbol);
    }
}

/**
 * Initialize a shell pipe buffer to an empty, non-overflowed state.
 *
 * Sets the buffer length to 0, clears the overflow flag, and writes an
 * empty string terminator into the data array.
 *
 * @param buffer Pointer to the shell_pipe_buffer to initialize.
 */
static void pipe_buffer_init(struct shell_pipe_buffer *buffer)
{
    buffer->length = 0;
    buffer->overflowed = false;
    buffer->data[0] = '\0';
}

/**
 * Append up to `len` bytes from `data` into a shell pipe buffer and null-terminate it.
 *
 * If the input would exceed the buffer's remaining capacity the write is truncated
 * to fit and the buffer's `overflowed` flag is set. The function is a no-op when
 * `context` or `data` is NULL or when `len` is zero.
 *
 * @param context Pointer to a `struct shell_pipe_buffer` to append into.
 * @param data Pointer to the bytes to append.
 * @param len Number of bytes to append; may be reduced to fit remaining capacity.
 */
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

/**
 * Write a byte range to the system TTY.
 *
 * Writes up to `len` bytes from `data` to the TTY; if `data` is NULL or `len` is zero the call is ignored.
 *
 * @param context Unused writer context (may be NULL).
 * @param data Pointer to bytes to write.
 * @param len Number of bytes to write from `data`.
 */
static void tty_writer(void *context, const char *data, size_t len)
{
    (void)context;
    if (!data || !len) {
        return;
    }
    tty_write(data, len);
}

/**
 * Write bytes to a shell IO writer by invoking its write callback.
 *
 * Polls for pending interrupts before calling `io->write`. Does nothing if
 * `io` is NULL, `io->write` is NULL, `data` is NULL, or `len` is zero.
 *
 * @param io   Shell IO descriptor containing the write callback and context.
 * @param data Pointer to the bytes to write.
 * @param len  Number of bytes to write from `data`.
 */
void shell_io_write(const struct shell_io *io, const char *data, size_t len)
{
    shell_interrupt_poll();
    if (!io || !io->write || !data || !len) {
        return;
    }
    io->write(io->context, data, len);
}

/**
 * Write a single character to the specified shell IO destination after polling for interrupts.
 *
 * Polls for pending keyboard events (including Ctrl-C) before invoking the IO write callback.
 *
 * @param io Destination IO descriptor whose write callback will be invoked.
 * @param c  Character to write.
 */
void shell_io_putc(const struct shell_io *io, char c)
{
    shell_interrupt_poll();
    shell_io_write(io, &c, 1u);
}

/**
 * Write a null-terminated string to the given shell I/O writer.
 *
 * If `str` is NULL, the function performs no action.
 *
 * @param io I/O writer context used as the destination for the string.
 * @param str Null-terminated string to write; may be NULL to indicate no output.
 */
void shell_io_write_string(const struct shell_io *io, const char *str)
{
    if (!str) {
        return;
    }
    shell_io_write(io, str, strlen(str));
}

/**
 * Poll the keyboard for pending characters and report whether a Ctrl-C interrupt is pending.
 *
 * If a Ctrl-C interrupt is pending and has not yet been announced, prints "^C\n" once and marks the interrupt as announced.
 *
 * @returns `true` if a Ctrl-C interrupt has been requested, `false` otherwise.
 */
bool shell_interrupt_poll(void)
{
    collect_background_input();

    if (shell_interrupt_requested && !shell_interrupt_announced) {
        tty_write_string("^C\n");
        shell_interrupt_announced = true;
    }

    return shell_interrupt_requested;
}

/**
 * Add a non-empty, non-blank line to the shell history ring buffer.
 *
 * If `line` is NULL, empty, or contains only spaces or tabs, it is ignored.
 * At most INPUT_BUFFER_SIZE - 1 characters are stored; the stored entry is null-terminated.
 * Advances the history head and increments the stored count up to HISTORY_SIZE.
 *
 * @param line Line to store in history.
 */
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

/**
 * Retrieve a history entry by offset from the most recent record.
 *
 * @param offset Number of entries back from the newest (0 = most recent).
 * @returns Pointer to the requested history string, or NULL if offset is out of range.
 */
static const char *history_get(size_t offset)
{
    if (offset >= history_count) {
        return 0;
    }
    size_t index = (history_head + HISTORY_SIZE - 1u - offset) % HISTORY_SIZE;
    return history[index];
}

/**
 * Get the next input character, using previously buffered background input if available.
 *
 * @returns The next character from the pending input buffer if present, otherwise the next character read from the keyboard.
 */
static char read_char_with_pending(void)
{
    char pending;
    if (pending_input_pop(&pending)) {
        return pending;
    }
    return keyboard_read_char();
}

/**
 * Reset the shell's interrupt state so subsequent operations proceed without a pending Ctrl-C.
 *
 * Clears the internal flags that indicate an interrupt was requested and whether the interrupt
 * has been announced to the user (`shell_interrupt_requested` and `shell_interrupt_announced`).
 */
static void shell_interrupt_reset_state(void)
{
    shell_interrupt_requested = false;
    shell_interrupt_announced = false;
}

/**
 * Handle interrupt signals and flag a pending Ctrl-C request.
 *
 * Sets the internal `shell_interrupt_requested` flag when `signal` is
 * `INTERRUPT_SIGNAL_CTRL_C`, leaving other signals unmodified.
 *
 * @param signal Interrupt signal delivered to the handler.
 * @param context Unused context pointer (ignored).
 */
static void shell_interrupt_handler(enum interrupt_signal signal, void *context)
{
    (void)context;
    if (signal != INTERRUPT_SIGNAL_CTRL_C) {
        return;
    }
    shell_interrupt_requested = true;
}

/**
 * Replace the current edit buffer with new text and update the displayed prompt.
 *
 * Copies up to capacity-1 bytes from `text` into `buffer`, NUL-terminates it,
 * updates `*len` to the resulting length, redraws the prompt and buffer content,
 * and erases any leftover characters from the previous display if `previous_len`
 * was longer than the new text.
 *
 * @param buffer Destination editing buffer to replace/display.
 * @param capacity Total size of `buffer` in bytes.
 * @param len Pointer to the current buffer length; updated to the new length.
 * @param text New NUL-terminated text to place into `buffer` (may be NULL to clear).
 * @param previous_len Length of the previously-displayed buffer used to erase leftover characters.
 */
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

/**
 * Compute the length of the common prefix shared by two null-terminated strings.
 * @param a First null-terminated string to compare.
 * @param b Second null-terminated string to compare.
 * @returns The number of initial characters that are identical in both strings.
 */
static size_t common_prefix_len(const char *a, const char *b)
{
    size_t len = 0;
    while (a[len] && b[len] && a[len] == b[len]) {
        ++len;
    }
    return len;
}

/**
 * Checks whether `name` begins with the provided `prefix`.
 *
 * @param name Null-terminated string to test as the candidate name.
 * @param prefix Prefix string to compare against `name`.
 * @param prefix_len Number of characters from `prefix` to match; zero means match any `name`.
 * @returns `true` if the first `prefix_len` characters of `name` are identical to `prefix`, `false` otherwise.
 */
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

/**
 * Check whether a buffer contains a space character within the first len bytes.
 *
 * @param buffer Buffer to examine.
 * @param len Maximum number of bytes to inspect from the start of buffer.
 * @returns `true` if a space character (' ') is found within the first `len` bytes of `buffer`, `false` otherwise.
 */
static bool buffer_has_space(const char *buffer, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == ' ') {
            return true;
        }
    }
    return false;
}

/**
 * Print the shell prompt and then echo exactly `len` characters from `buffer`.
 *
 * @param buffer Pointer to the character buffer to display; may not be null-terminated.
 * @param len Number of characters from `buffer` to write after the prompt.
 */
static void redraw_prompt_with_buffer(const char *buffer, size_t len)
{
    prompt();
    for (size_t i = 0; i < len; ++i) {
        tty_putc(buffer[i]);
    }
}

/**
 * Print all command names that match the given input prefix and restore the prompt.
 *
 * Writes a newline, then each command name whose first `len` characters equal `buffer`
 * (an empty prefix matches all) on its own line, and finally redraws the prompt
 * with `buffer` as the current input.
 *
 * @param buffer Pointer to the input buffer containing the prefix to match.
 * @param len Number of bytes from `buffer` to use as the prefix.
 * @param commands Array of command pointers to search.
 * @param command_count Number of entries in `commands`.
 */
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

/**
 * Perform tab completion for the current input buffer against available commands.
 *
 * Attempts to complete the word in buffer using the command names in commands.
 * If the buffer is empty, prints a list of all commands. If the buffer contains
 * whitespace, emits a bell and does nothing. If one or more commands match the
 * current buffer prefix, appends any longer common prefix that fits into the
 * buffer and echoes the characters to the TTY. If exactly one command matches
 * fully, appends a trailing space (when space remains) and echoes it. If no
 * matches are found, emits a bell. When multiple matches exist and no further
 * characters can be added, lists the matching names.
 *
 * @param buffer Mutable NUL-terminated input buffer; will be modified when
 *               characters are appended.
 * @param len    Pointer to current length of the text in buffer; updated if
 *               characters are appended.
 * @param capacity Total capacity of buffer including space for the terminating NUL.
 * @param commands Array of pointers to available shell_command structures.
 * @param command_count Number of entries in commands.
 */
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

/**
 * Read a single edited input line from the keyboard into the provided buffer.
 *
 * Supports in-line editing, backspace, history navigation (up/down), tab completion using
 * `commands` (may be NULL), and finishes when a newline is entered or the buffer capacity
 * is reached. Carriage returns ('\r') are ignored.
 *
 * @param buffer Destination buffer for the entered line; the result is NUL-terminated.
 * @param capacity Maximum size of `buffer` in bytes, including the terminating NUL.
 * @param commands Array of pointers to available commands used for tab-completion (may be NULL).
 * @param command_count Number of entries in `commands`.
 * @returns The length of the entered line in bytes, not counting the terminating NUL. Returns `0`
 *          when input was interrupted by Ctrl-C (the buffer will be an empty string). */
static size_t read_line(char *buffer, size_t capacity, const struct shell_command *const *commands, size_t command_count)
{
    size_t len = 0;
    int history_offset = -1;
    bool saved_current_valid = false;
    char saved_current[INPUT_BUFFER_SIZE];

    while (len + 1 < capacity) {
        char c = read_char_with_pending();
        if (!c) {
            continue;
        }

        if ((unsigned char)c == (unsigned char)SHELL_CTRL_C) {
            tty_write_string("^C\n");
            buffer[0] = '\0';
            len = 0;
            history_offset = -1;
            saved_current_valid = false;
            return 0;
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

/**
 * Parse an input line into trimmed pipeline segments separated by '|' and populate the segments array.
 *
 * The function modifies the input `line` in-place by replacing pipe characters and trailing segment
 * whitespace with NUL terminators, sets `segments` to point at each segment's start, and sets
 * `*segment_count` to the number of parsed segments.
 *
 * @param line Mutable NUL-terminated input string containing one or more commands possibly joined with '|'.
 * @param segments Caller-provided array that will receive pointers to each segment within `line`.
 * @param segment_count Pointer that will be set to the number of segments written into `segments`.
 * @returns `true` if at least one non-empty segment was parsed and `segments`/`*segment_count` were populated;
 *          `false` on parse error (empty input, empty segment, trailing pipe, or too many segments).
 */
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

/**
 * Execute a sequence of pipeline segments by running each segment's command and forwarding intermediate output between stages.
 *
 * Each entry in `segments` is tokenized and looked up in `commands`; for intermediate stages output is captured and supplied
 * as input to the next stage, and the final stage writes to the TTY. Errors and informational messages are written to the TTY.
 *
 * @param segments Array of null-terminated strings, one per pipeline segment (each segment is a full command line).
 * @param segment_count Number of entries in `segments`.
 * @param commands Array of available `shell_command` pointers used for command lookup.
 * @param command_count Number of entries in `commands`.
 * @returns `true` if all pipeline segments were executed successfully, `false` if execution failed (e.g., empty segment or unknown command).
 */
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

        if (shell_interrupt_poll()) {
            return false;
        }

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
 * Start and run the interactive shell until it exits.
 *
 * Initializes the registered builtin commands and, if none are available, writes
 * an error to the TTY and returns. Otherwise prints a startup hint and enters a
 * loop that prompts for user input, records non-empty lines in history,
 * parses the line into pipeline segments, and executes the resulting commands
 * until the shell is terminated.
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

    if (shell_interrupt_subscription < 0) {
        shell_interrupt_subscription = interrupt_subscribe(INTERRUPT_SIGNAL_CTRL_C, shell_interrupt_handler, 0);
    }

    tty_write_string("Type 'help' for a list of commands.\n");

    for (;;) {
        shell_interrupt_reset_state();
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
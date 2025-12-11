#include <lux/fs.h>
#include <lux/keyboard.h>
#include <lux/memory.h>
#include <lux/printf.h>
#include <lux/shell.h>
#include <lux/tty.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define LESS_CTRL_C ((char)0x03)
#define LESS_READ_CHUNK 512u
#define LESS_STATUS_BUFFER 128u

struct less_document {
    char *data;
    size_t length;
    char **lines;
    size_t line_count;
    const char *label;
};

/**
 * Write usage instructions for the `less` shell command to the given shell I/O.
 *
 * Outputs a brief invocation line and a short description explaining that `less`
 * accepts a file path or piped input for paging.
 */
static void less_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: less <path>\n");
    shell_io_write_string(io, "Provide a path or pipe data into less for paging.\n");
}

/**
 * Print a standardized error message for the less command.
 *
 * The message is written to the provided shell I/O and formatted as:
 * "less: <subject>: <reason>\n". If `subject` is NULL, "<input>" is used.
 *
 * @param subject Subject of the error shown after the "less: " prefix; may be NULL.
 * @param reason  Short explanation of the error to display.
 */
static void less_print_error(const struct shell_io *io, const char *subject, const char *reason)
{
    shell_io_write_string(io, "less: ");
    shell_io_write_string(io, subject ? subject : "<input>");
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

/**
 * Load a file from the filesystem into a newly allocated, null-terminated buffer.
 *
 * On success the function allocates a buffer containing the file contents (with
 * an added terminating NUL), sets `*out_data` to the buffer and `*out_len` to
 * the number of bytes read. On failure an error message is written to `io`.
 *
 * @param path Path to the file to load.
 * @param out_data Pointer that receives the allocated buffer on success; the
 *                 caller is responsible for freeing it.
 * @param out_len  Pointer that receives the number of bytes read (excluding the
 *                 terminating NUL) on success.
 * @param io       Shell I/O used to report errors.
 * @returns `true` if the file was loaded and outputs were set, `false` otherwise.
 */
static bool less_load_file(const char *path, char **out_data, size_t *out_len, const struct shell_io *io)
{
    char resolved[SHELL_PATH_MAX];
    if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
        less_print_error(io, path, "path too long");
        return false;
    }

    if (!fs_ready()) {
        shell_io_write_string(io, "less: filesystem not available\n");
        return false;
    }

    struct fs_stat stats;
    if (!fs_stat_path(resolved, &stats)) {
        less_print_error(io, path, "not found");
        return false;
    }

    if (stats.is_dir) {
        less_print_error(io, path, "is a directory");
        return false;
    }

    size_t capacity = stats.size + 1u;
    char *buffer = (char *)malloc(capacity ? capacity : 1u);
    if (!buffer) {
        shell_io_write_string(io, "less: out of memory\n");
        return false;
    }

    size_t offset = 0;
    while (offset < stats.size) {
        size_t chunk = stats.size - offset;
        if (chunk > LESS_READ_CHUNK) {
            chunk = LESS_READ_CHUNK;
        }

        size_t bytes_read = 0;
        if (!fs_read(resolved, offset, buffer + offset, chunk, &bytes_read)) {
            free(buffer);
            less_print_error(io, path, "read error");
            return false;
        }

        if (!bytes_read) {
            break;
        }

        offset += bytes_read;
    }

    buffer[offset] = '\0';
    *out_data = buffer;
    *out_len = offset;
    return true;
}

/**
 * Copy the shell's input buffer into a newly allocated, NUL-terminated memory buffer.
 *
 * Allocates memory to hold the input from `io->input`, copies the bytes, appends a
 * terminating NUL, and returns the buffer and length via the output parameters.
 * The caller is responsible for freeing the returned buffer.
 *
 * @param io Shell I/O structure containing an input buffer to copy.
 * @param out_data Pointer to receive the allocated, NUL-terminated buffer on success.
 * @param out_len Pointer to receive the length of the copied input in bytes (excluding the terminating NUL).
 * @returns `true` if the input was present and successfully copied; `false` if there was no input or allocation failed.
 */
static bool less_copy_input(const struct shell_io *io, char **out_data, size_t *out_len)
{
    if (!io || !io->input_len) {
        return false;
    }

    size_t length = io->input_len;
    char *buffer = (char *)malloc(length + 1u);
    if (!buffer) {
        shell_io_write_string(io, "less: out of memory\n");
        return false;
    }

    memcpy(buffer, io->input, length);
    buffer[length] = '\0';
    *out_data = buffer;
    *out_len = length;
    return true;
}

/**
 * Split the document's raw data into NUL-terminated lines and populate the document's line array and count.
 *
 * This function allocates and assigns doc->lines, replaces carriage returns and newline characters
 * in doc->data with NUL ('\0'), and sets doc->line_count and doc->lines to point at the start of each line.
 * The caller is responsible for freeing doc->lines and the original doc->data when no longer needed.
 *
 * @param doc Pointer to a less_document whose data buffer contains the text to split; doc->data must be non-NULL.
 * @return `true` if lines were prepared and doc->lines populated successfully, `false` on invalid input or allocation failure.
 */
static bool less_prepare_lines(struct less_document *doc)
{
    if (!doc || !doc->data) {
        return false;
    }

    size_t capacity = (doc->length ? doc->length : 1u) + 1u;
    doc->lines = (char **)malloc(capacity * sizeof(char *));
    if (!doc->lines) {
        return false;
    }

    size_t count = 0;
    doc->lines[count++] = doc->data;

    for (size_t i = 0; i < doc->length; ++i) {
        if (doc->data[i] == '\r') {
            doc->data[i] = '\0';
            continue;
        }

        if (doc->data[i] == '\n') {
            doc->data[i] = '\0';
            if (i + 1u < doc->length) {
                doc->lines[count++] = &doc->data[i + 1u];
            } else {
                doc->lines[count++] = doc->data + doc->length;
            }
        }
    }

    if (!doc->length) {
        doc->lines[0] = doc->data;
        count = 1;
    }

    doc->line_count = count;
    return true;
}

/**
 * Render a page of the document to the TTY and draw a status line.
 *
 * Clears the terminal, writes up to `viewport_rows` lines starting at `top_line`
 * (truncating each line to `cols` if non-zero), and then writes a status line at
 * `status_row` showing the document label, visible line range, total lines,
 * and percentage progress. Pads or truncates the status line to fit `cols`
 * when `cols` is non-zero.
 *
 * @param doc Document containing loaded text and line pointers.
 * @param top_line Index of the first line to display (0-based).
 * @param viewport_rows Number of text rows available for document lines.
 * @param status_row Row index where the status line should be rendered.
 * @param cols Maximum columns per row; if zero, lines are not truncated/padded.
 */
static void less_render_page(const struct less_document *doc, size_t top_line, size_t viewport_rows, size_t status_row, size_t cols)
{
    tty_clear();

    for (size_t row = 0; row < viewport_rows; ++row) {
        tty_set_cursor_position(row, 0);
        size_t line_index = top_line + row;
        if (line_index < doc->line_count) {
            const char *line = doc->lines[line_index];
            size_t len = strlen(line);
            if (cols && len > cols) {
                len = cols;
            }
            if (len) {
                tty_write(line, len);
            }
        }
    }

    tty_set_cursor_position(status_row, 0);
    size_t start_line = doc->line_count ? (top_line + 1u) : 0u;
    size_t end_line = doc->line_count ? ((top_line + viewport_rows) < doc->line_count ? (top_line + viewport_rows) : doc->line_count) : 0u;
    size_t percent = doc->line_count ? (end_line * 100u) / doc->line_count : 100u;

    char status[LESS_STATUS_BUFFER];
    const char *label = doc->label ? doc->label : "<input>";
    snprintf(status, sizeof(status), "[less] %s  %zu-%zu/%zu  %zu%%  (q=quit, space=down, b=up)",
             label, start_line, end_line, doc->line_count, percent);

    size_t status_len = strlen(status);
    if (cols && status_len > cols) {
        status[cols] = '\0';
        status_len = cols;
    }

    tty_write(status, status_len);
    if (cols && status_len < cols) {
        size_t padding = cols - status_len;
        for (size_t i = 0; i < padding; ++i) {
            tty_putc(' ');
        }
    }
}

/**
 * Waits until a keyboard character is available or the shell requests stop.
 *
 * Blocks by polling for input while checking for a shell stop request.
 *
 * @returns The character read from the keyboard, or `0` if a shell stop was requested.
 */
static char less_wait_key(void)
{
    char symbol = 0;
    while (!symbol) {
        if (shell_command_should_stop()) {
            return 0;
        }
        if (keyboard_poll_char(&symbol)) {
            break;
        }
    }
    return symbol;
}

/**
 * Display and interactively page through a loaded document on the TTY.
 *
 * Renders pages according to the current terminal size, updates the visible
 * range in response to navigation keys (Space = page down, Enter/Down = one
 * line down, Up/k = one line up, b = page up, q/Q or Ctrl-C = quit), shows a
 * status line, and clears the terminal when the viewer exits.
 * @param doc Document containing loaded text and line index used for paging.
 */
static void less_view_document(struct less_document *doc)
{
    size_t total_rows = tty_rows();
    if (!total_rows) {
        total_rows = 1u;
    }

    size_t viewport_rows = (total_rows > 1u) ? (total_rows - 1u) : total_rows;
    if (!viewport_rows) {
        viewport_rows = 1u;
    }

    size_t status_row = (total_rows > viewport_rows) ? viewport_rows : (viewport_rows - 1u);
    size_t cols = tty_cols();
    size_t top_line = 0;
    bool running = true;

    while (running) {
        if (shell_command_should_stop()) {
            break;
        }
        size_t max_start = (doc->line_count > viewport_rows) ? (doc->line_count - viewport_rows) : 0u;
        if (top_line > max_start) {
            top_line = max_start;
        }

        less_render_page(doc, top_line, viewport_rows, status_row, cols);
        char key = less_wait_key();

        if ((unsigned char)key == (unsigned char)LESS_CTRL_C) {
            break;
        }

        switch (key) {
        case 'q':
        case 'Q':
            running = false;
            break;
        case ' ':
            if (top_line < max_start) {
                size_t step = viewport_rows;
                size_t remaining = max_start - top_line;
                if (step > remaining) {
                    step = remaining;
                }
                if (!step) {
                    step = 1u;
                }
                top_line += step;
            } else {
                running = false;
            }
            break;
        case '\n':
        case '\r':
        case KEYBOARD_KEY_ARROW_DOWN:
            if (top_line < max_start) {
                ++top_line;
            }
            break;
        case KEYBOARD_KEY_ARROW_UP:
        case 'k':
        case 'K':
            if (top_line > 0) {
                --top_line;
            }
            break;
        case 'b':
        case 'B':
            if (top_line > 0) {
                if (top_line > viewport_rows) {
                    top_line -= viewport_rows;
                } else {
                    top_line = 0;
                }
            }
            break;
        default:
            break;
        }
    }

    tty_clear();
}

/**
 * Handle the "less" shell command: load text from a file or from piped input,
 * present it interactively in a pager on the terminal, and release resources on exit.
 *
 * If a path argument is provided (argc >= 2) the function attempts to load that
 * file; otherwise it reads from the shell's input buffer. If neither source is
 * available, usage information is written to the shell. Errors (file access,
 * read failures, or out-of-memory) are reported to the shell. The pager runs
 * until the user quits, after which all allocated memory is freed.
 *
 * @param argc Number of command-line arguments; if >= 2 the second argument is treated as the file path to view.
 * @param argv Command-line argument array.
 * @param io   Shell I/O context used for input (piped data) and output; may be NULL.
 */
static void less_handler(int argc, char **argv, const struct shell_io *io)
{
    char *data = 0;
    size_t length = 0;
    char label_buffer[LESS_STATUS_BUFFER];
    const char *label = "<stdin>";

    if (argc >= 2) {
        const char *path = argv[1];
        size_t label_len = strlen(path);
        if (label_len >= sizeof(label_buffer)) {
            label_len = sizeof(label_buffer) - 1u;
        }
        memcpy(label_buffer, path, label_len);
        label_buffer[label_len] = '\0';
        label = label_buffer;

        if (!less_load_file(path, &data, &length, io)) {
            return;
        }
    } else if (io && io->input_len) {
        if (!less_copy_input(io, &data, &length)) {
            return;
        }
        label = "<stdin>";
    } else {
        less_usage(io);
        return;
    }

    struct less_document doc = {
        .data = data,
        .length = length,
        .lines = 0,
        .line_count = 0,
        .label = label
    };

    if (!less_prepare_lines(&doc)) {
        shell_io_write_string(io, "less: out of memory\n");
        free(data);
        return;
    }

    less_view_document(&doc);

    free(doc.lines);
    free(doc.data);
}

const struct shell_command shell_command_less = {
    .name = "less",
    .help = "Page through text with scrolling",
    .handler = less_handler,
};
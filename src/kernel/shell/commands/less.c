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

static void less_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: less <path>\n");
    shell_io_write_string(io, "Provide a path or pipe data into less for paging.\n");
}

static void less_print_error(const struct shell_io *io, const char *subject, const char *reason)
{
    shell_io_write_string(io, "less: ");
    shell_io_write_string(io, subject ? subject : "<input>");
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

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

static char less_wait_key(void)
{
    char symbol = 0;
    while (!symbol) {
        shell_interrupt_poll();
        if (keyboard_poll_char(&symbol)) {
            break;
        }
    }
    return symbol;
}

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
        shell_interrupt_poll();
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

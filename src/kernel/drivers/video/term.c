/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Off-screen terminal surface implementation for editor-style UIs.
 */
#include <lux/term.h>

#include <lux/memory.h>
#include <string.h>

static size_t term_cell_index(const struct term_surface *surface, size_t row, size_t col)
{
    return row * surface->cols + col;
}

static bool term_surface_valid_cell(const struct term_surface *surface, size_t row, size_t col)
{
    return surface && row < surface->rows && col < surface->cols;
}

static void term_surface_reset_dimensions(struct term_surface *surface)
{
    surface->rows = tty_rows();
    surface->cols = tty_cols();
    if (surface->cursor_row >= surface->rows) {
        surface->cursor_row = surface->rows ? surface->rows - 1u : 0u;
    }
    if (surface->cursor_col >= surface->cols) {
        surface->cursor_col = surface->cols ? surface->cols - 1u : 0u;
    }
}

static bool term_surface_allocate_cells(struct term_surface *surface)
{
    size_t total = surface->rows * surface->cols;
    surface->cells = calloc(total, sizeof(struct tty_cell));
    return surface->cells != 0;
}

struct term_surface *term_surface_create(uint8_t color)
{
    struct term_surface *surface = calloc(1u, sizeof(struct term_surface));
    if (!surface) {
        return 0;
    }

    surface->default_color = color;
    term_surface_reset_dimensions(surface);
    if (!term_surface_allocate_cells(surface)) {
        term_surface_destroy(surface);
        return 0;
    }

    term_surface_clear(surface, ' ');
    return surface;
}

void term_surface_destroy(struct term_surface *surface)
{
    if (!surface) {
        return;
    }

    if (surface->cells) {
        free(surface->cells);
    }

    free(surface);
}

bool term_surface_resize(struct term_surface *surface, size_t rows, size_t cols)
{
    if (!surface || !rows || !cols) {
        return false;
    }

    struct tty_cell *old_cells = surface->cells;
    size_t old_rows = surface->rows;
    size_t old_cols = surface->cols;

    surface->rows = rows;
    surface->cols = cols;
    surface->cells = calloc(rows * cols, sizeof(struct tty_cell));
    if (!surface->cells) {
        surface->cells = old_cells;
        surface->rows = old_rows;
        surface->cols = old_cols;
        return false;
    }

    size_t min_rows = (rows < old_rows) ? rows : old_rows;
    size_t min_cols = (cols < old_cols) ? cols : old_cols;
    for (size_t r = 0; r < min_rows; ++r) {
        memcpy(&surface->cells[r * cols], &old_cells[r * old_cols], min_cols * sizeof(struct tty_cell));
    }

    free(old_cells);
    if (surface->cursor_row >= surface->rows) {
        surface->cursor_row = surface->rows - 1u;
    }
    if (surface->cursor_col >= surface->cols) {
        surface->cursor_col = surface->cols - 1u;
    }
    return true;
}

void term_surface_clear(struct term_surface *surface, char fill_char)
{
    if (!surface || !surface->cells) {
        return;
    }

    struct tty_cell cell = {
        .character = fill_char,
        .color = surface->default_color
    };

    size_t total = surface->rows * surface->cols;
    for (size_t i = 0; i < total; ++i) {
        surface->cells[i] = cell;
    }
}

void term_surface_set_cursor(struct term_surface *surface, size_t row, size_t col)
{
    if (!surface) {
        return;
    }
    if (row >= surface->rows) {
        row = surface->rows ? surface->rows - 1u : 0u;
    }
    if (col >= surface->cols) {
        col = surface->cols ? surface->cols - 1u : 0u;
    }
    surface->cursor_row = row;
    surface->cursor_col = col;
}

void term_surface_draw_char(struct term_surface *surface, size_t row, size_t col, char c, uint8_t color)
{
    if (!term_surface_valid_cell(surface, row, col)) {
        return;
    }
    struct tty_cell *cell = &surface->cells[term_cell_index(surface, row, col)];
    cell->character = c;
    cell->color = color;
}

void term_surface_write_string(struct term_surface *surface, size_t row, size_t col, const char *text, uint8_t color)
{
    if (!surface || !text) {
        return;
    }

    size_t cursor_col = col;
    size_t cursor_row = row;
    for (size_t i = 0; text[i] && cursor_row < surface->rows; ++i) {
        if (text[i] == '\n') {
            cursor_col = col;
            ++cursor_row;
            continue;
        }

        if (cursor_col >= surface->cols) {
            cursor_col = col;
            ++cursor_row;
            if (cursor_row >= surface->rows) {
                break;
            }
        }

        term_surface_draw_char(surface, cursor_row, cursor_col, text[i], color);
        ++cursor_col;
    }
}

void term_surface_fill_rect(struct term_surface *surface, size_t row, size_t col, size_t height, size_t width, char c, uint8_t color)
{
    if (!surface || !surface->cells) {
        return;
    }

    for (size_t r = 0; r < height && (row + r) < surface->rows; ++r) {
        for (size_t cl = 0; cl < width && (col + cl) < surface->cols; ++cl) {
            term_surface_draw_char(surface, row + r, col + cl, c, color);
        }
    }
}

void term_surface_scroll_region(struct term_surface *surface, size_t top_row, size_t bottom_row, int delta_rows, char fill_char)
{
    if (!surface || !surface->cells) {
        return;
    }

    if (top_row >= surface->rows) {
        return;
    }

    if (bottom_row >= surface->rows) {
        bottom_row = surface->rows - 1u;
    }

    if (bottom_row <= top_row) {
        return;
    }

    size_t region_height = bottom_row - top_row + 1u;
    size_t columns = surface->cols;
    struct tty_cell fill = {
        .character = fill_char,
        .color = surface->default_color
    };

    if (delta_rows > 0) {
        size_t shift = (size_t)delta_rows;
        if (shift >= region_height) {
            for (size_t r = top_row; r <= bottom_row; ++r) {
                for (size_t c = 0; c < columns; ++c) {
                    surface->cells[term_cell_index(surface, r, c)] = fill;
                }
            }
            return;
        }

        for (size_t r = top_row; r + shift <= bottom_row; ++r) {
            size_t dst_row = r;
            size_t src_row = r + shift;
            memcpy(&surface->cells[dst_row * columns], &surface->cells[src_row * columns], columns * sizeof(struct tty_cell));
        }

        for (size_t r = bottom_row + 1u - shift; r <= bottom_row; ++r) {
            for (size_t c = 0; c < columns; ++c) {
                surface->cells[term_cell_index(surface, r, c)] = fill;
            }
        }
    } else if (delta_rows < 0) {
        size_t shift = (size_t)(-delta_rows);
        if (shift >= region_height) {
            for (size_t r = top_row; r <= bottom_row; ++r) {
                for (size_t c = 0; c < columns; ++c) {
                    surface->cells[term_cell_index(surface, r, c)] = fill;
                }
            }
            return;
        }

        for (size_t r = bottom_row + 1u; r-- > top_row + shift;) {
            size_t dst_row = r;
            size_t src_row = r - shift;
            memcpy(&surface->cells[dst_row * columns], &surface->cells[src_row * columns], columns * sizeof(struct tty_cell));
        }

        for (size_t r = top_row; r < top_row + shift; ++r) {
            for (size_t c = 0; c < columns; ++c) {
                surface->cells[term_cell_index(surface, r, c)] = fill;
            }
        }
    }
}

static void term_surface_write_cell_to_tty(size_t row, size_t col, const struct tty_cell *cell)
{
    tty_write_cell(row, col, cell->character, cell->color);
}

void term_surface_flush_region(const struct term_surface *surface, size_t row, size_t col, size_t height, size_t width)
{
    if (!surface || !surface->cells) {
        return;
    }

    size_t max_row = row + height;
    size_t max_col = col + width;

    if (max_row > surface->rows) {
        max_row = surface->rows;
    }
    if (max_col > surface->cols) {
        max_col = surface->cols;
    }

    for (size_t r = row; r < max_row; ++r) {
        for (size_t c = col; c < max_col; ++c) {
            const struct tty_cell *cell = &surface->cells[term_cell_index(surface, r, c)];
            char current_char = tty_get_cell_character(r, c);
            uint8_t current_color = tty_get_cell_color(r, c);
            if (current_char != cell->character || current_color != cell->color) {
                term_surface_write_cell_to_tty(r, c, cell);
            }
        }
    }
}

void term_surface_flush(const struct term_surface *surface)
{
    if (!surface) {
        return;
    }

    term_surface_flush_region(surface, 0, 0, surface->rows, surface->cols);
    tty_set_cursor_position(surface->cursor_row, surface->cursor_col);
}
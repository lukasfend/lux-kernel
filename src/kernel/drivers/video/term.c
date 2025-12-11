/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Off-screen terminal surface implementation for editor-style UIs.
 */
#include <lux/term.h>
#include <lux/tty.h>

#include <lux/memory.h>
#include <string.h>

/**
 * Compute the linear index for a cell at the given row and column within the surface's cell array.
 *
 * Assumes `surface` is non-NULL and `row`/`col` are within the surface bounds.
 *
 * @returns The zero-based index into `surface->cells` corresponding to (row, col).
 */
static size_t term_cell_index(const struct term_surface *surface, size_t row, size_t col)
{
    return row * surface->cols + col;
}

/**
 * Check whether the specified row and column identify a valid cell on the surface.
 *
 * @param surface Pointer to the term_surface to validate against.
 * @param row Zero-based row index to check.
 * @param col Zero-based column index to check.
 * @returns `true` if `surface` is non-null and `row` is less than `surface->rows` and `col` is less than `surface->cols`, `false` otherwise.
 */
static bool term_surface_valid_cell(const struct term_surface *surface, size_t row, size_t col)
{
    return surface && row < surface->rows && col < surface->cols;
}

/**
 * Update the surface dimensions to match the current TTY size and clamp the cursor to the new bounds.
 *
 * @param surface Surface whose rows, cols, and cursor positions will be updated.
 */
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

/**
 * Allocate and initialize the cell buffer for the given surface to hold
 * surface->rows × surface->cols tty_cell entries.
 *
 * @param surface Surface whose cells field will be allocated and initialized.
 * @returns `true` if allocation succeeded and surface->cells was set, `false` otherwise.
 */
static bool term_surface_allocate_cells(struct term_surface *surface)
{
    size_t total = surface->rows * surface->cols;
    surface->cells = calloc(total, sizeof(struct tty_cell));
    return surface->cells != 0;
}

/**
 * Allocate and initialize a new off-screen terminal surface using the given default color.
 *
 * @param color Default color attribute applied to every cell on the surface.
 * @returns Pointer to the newly allocated term_surface, or NULL if allocation failed.
 *          The returned surface must be released with term_surface_destroy().
 */
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

/**
 * Free a term_surface and its associated cell storage.
 *
 * If `surface` is NULL the function is a no-op. Any allocated internal
 * cell buffer is freed before the surface structure itself is freed.
 *
 * @param surface The surface to destroy; may be NULL. After return the
 *                pointer is no longer valid.
 */
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

/**
 * Resize the term_surface to the specified number of rows and columns, reallocating its cell buffer.
 *
 * If allocation succeeds, the contents of the overlapping region from the previous buffer are copied
 * into the new buffer, the old buffer is freed, and the cursor row/column are clamped to the new bounds.
 * If allocation fails or inputs are invalid, the surface is left unchanged.
 *
 * @param surface The surface to resize.
 * @param rows The new number of rows.
 * @param cols The new number of columns.
 * @returns `true` if the surface was resized and reallocated successfully, `false` otherwise.
 */
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

/**
 * Fill every cell of the surface with a given character using the surface's default color.
 *
 * Does nothing if `surface` is NULL or if the surface has no allocated cell buffer.
 *
 * @param surface Surface whose cells will be filled.
 * @param fill_char Character to write into every cell.
 */
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

/**
 * Set the surface's cursor position, clamping the coordinates to valid bounds.
 *
 * If `surface` is NULL this function does nothing. If `row` or `col` is
 * greater than or equal to the surface dimensions they are clamped to the
 * last valid index for that dimension (or 0 if the dimension size is 0).
 *
 * @param surface Surface whose cursor will be updated; no-op if NULL.
 * @param row Desired cursor row (will be clamped into [0, surface->rows-1]).
 * @param col Desired cursor column (will be clamped into [0, surface->cols-1]).
 */
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

/**
 * Write a character and its color into the surface cell at the specified row and column.
 *
 * If the surface is null or the coordinates are outside the surface bounds, the call does nothing.
 *
 * @param surface Surface to modify.
 * @param row Row index of the target cell.
 * @param col Column index of the target cell.
 * @param c Character to store in the cell.
 * @param color Color attribute to assign to the cell.
 */
void term_surface_draw_char(struct term_surface *surface, size_t row, size_t col, char c, uint8_t color)
{
    if (!term_surface_valid_cell(surface, row, col)) {
        return;
    }
    struct tty_cell *cell = &surface->cells[term_cell_index(surface, row, col)];
    cell->character = c;
    cell->color = color;
}

/**
 * Write a NUL-terminated string into the surface starting at a specified position.
 *
 * Characters are written sequentially beginning at (row, col). A newline ('\n')
 * resets the column to the initial start column and advances to the next row.
 * When the column reaches the surface width the write wraps to the next row.
 * Writing stops when the end of the string is reached or when the write would
 * advance past the last surface row. This function is a no-op if `surface` or
 * `text` is NULL.
 *
 * @param surface Target term_surface to write into.
 * @param row Starting row for the write operation.
 * @param col Starting column for the write operation; used as the reset column on newlines and wrapping.
 * @param text NUL-terminated string to write.
 * @param color Color attribute to apply to each written cell.
 */
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

/**
 * Fill a rectangular region of the surface with a character and color.
 *
 * The rectangle's top-left corner is at (row, col). Filling is clamped to
 * the surface bounds; if the surface or its cell storage is NULL the call
 * is a no-op.
 *
 * @param surface Surface to modify.
 * @param row Top row of the rectangle.
 * @param col Left column of the rectangle.
 * @param height Number of rows to fill (clamped to surface height - row).
 * @param width Number of columns to fill (clamped to surface width - col).
 * @param c Character to write into each cell of the region.
 * @param color Color attribute to apply to each written cell.
 */
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

/**
 * Scrolls a vertical region of the off-screen surface by a number of rows and fills vacated lines.
 *
 * Shifts rows in the inclusive range [top_row, bottom_row] upward when `delta_rows` > 0
 * or downward when `delta_rows` < 0. If the absolute shift is greater than or equal to
 * the region height the entire region is filled with `fill_char` painted with the surface's
 * default color. The function clamps `bottom_row` to the surface height and no-ops when
 * inputs are invalid (null surface, no cell storage, out-of-range top_row, or empty region).
 *
 * @param surface The target term_surface to modify.
 * @param top_row The topmost row of the region to scroll (inclusive).
 * @param bottom_row The bottommost row of the region to scroll (inclusive); values
 *                   greater than the surface height are clamped to the last row.
 * @param delta_rows Positive to scroll the region upward, negative to scroll downward.
 * @param fill_char Character used to fill lines exposed by the scroll; filled cells use
 *                  the surface's default color.
 */
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

/**
 * Write a surface cell to the terminal at the specified coordinates.
 *
 * @param row Destination row on the terminal.
 * @param col Destination column on the terminal.
 * @param cell Pointer to the `tty_cell` whose character and color will be written.
 */
static void term_surface_write_cell_to_tty(size_t row, size_t col, const struct tty_cell *cell)
{
    tty_write_cell(row, col, cell->character, cell->color);
}

/**
 * Flush a rectangular sub-region of the off-screen surface to the real terminal.
 *
 * Compares each cell in the specified region with the corresponding on-screen cell
 * and writes only cells that differ to the terminal. The region is clamped to the
 * surface bounds; no action is taken if `surface` or its cell storage is NULL.
 *
 * @param surface Surface to flush from.
 * @param row     Index of the first row of the region to flush.
 * @param col     Index of the first column of the region to flush.
 * @param height  Number of rows in the region to flush; region is clamped to surface height.
 * @param width   Number of columns in the region to flush; region is clamped to surface width.
 */
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

/**
 * Flushes the entire off-screen terminal surface to the real terminal and updates the terminal cursor to the surface's cursor position.
 *
 * If `surface` is NULL, the call has no effect.
 *
 * @param surface Off-screen terminal surface to flush.
 */
void term_surface_flush(const struct term_surface *surface)
{
    if (!surface) {
        return;
    }

    term_surface_flush_region(surface, 0, 0, surface->rows, surface->cols);
    tty_set_cursor_position(surface->cursor_row, surface->cursor_col);
}
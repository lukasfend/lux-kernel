/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: RAM-backed swapfiles that can mirror on-disk data as needed.
 */
#include <lux/swap.h>

#include <lux/fs.h>
#include <lux/memory.h>
#include <string.h>

/**
 * Ensure the swap_file has at least `min_capacity` bytes allocated.
 *
 * If the current capacity is smaller, this function allocates a new buffer
 * (growing from 512 bytes and doubling until >= `min_capacity`), copies any
 * existing contents, frees the old buffer, and updates `swap->data` and
 * `swap->capacity`.
 *
 * @param swap Pointer to the swap_file to grow.
 * @param min_capacity Minimum required capacity in bytes.
 * @returns `true` on success, `false` if `swap` is NULL or memory allocation fails.
 */
static bool swap_file_grow(struct swap_file *swap, size_t min_capacity)
{
    if (!swap) {
        return false;
    }

    if (swap->capacity >= min_capacity) {
        return true;
    }

    size_t new_capacity = swap->capacity ? swap->capacity : 512u;
    while (new_capacity < min_capacity) {
        new_capacity *= 2u;
    }

    uint8_t *new_data = malloc(new_capacity);
    if (!new_data) {
        return false;
    }

    if (swap->data && swap->size) {
        memcpy(new_data, swap->data, swap->size);
    }

    if (swap->data) {
        free(swap->data);
    }

    swap->data = new_data;
    swap->capacity = new_capacity;
    return true;
}

/**
 * Initialize a swap_file structure and optionally reserve initial capacity.
 * @param swap Pointer to the swap_file to initialize; must not be NULL.
 * @param reserve_bytes Number of bytes to reserve for the internal buffer; 0 means no reservation.
 * @returns `true` if the structure was initialized (and reservation succeeded when requested), `false` if `swap` is NULL or the reservation failed.
 */
bool swap_file_init(struct swap_file *swap, size_t reserve_bytes)
{
    if (!swap) {
        return false;
    }

    swap->data = 0;
    swap->size = 0;
    swap->capacity = 0;
    if (reserve_bytes) {
        return swap_file_grow(swap, reserve_bytes);
    }
    return true;
}

/**
 * Release resources held by a swap_file and reset it to an empty state.
 *
 * Frees the internal data buffer if present and sets `data` to NULL,
 * and `size` and `capacity` to zero. Safe to call with a NULL pointer.
 *
 * @param swap swap_file instance to free and reset.
 */
void swap_file_free(struct swap_file *swap)
{
    if (!swap) {
        return;
    }

    if (swap->data) {
        free(swap->data);
    }
    swap->data = 0;
    swap->size = 0;
    swap->capacity = 0;
}

/**
 * Ensure a swap_file has capacity for at least new_capacity bytes.
 *
 * @param swap Swap file to resize.
 * @param new_capacity Desired minimum capacity in bytes.
 * @returns `true` if the swap has at least `new_capacity` bytes allocated (or was successfully grown), `false` otherwise.
 */
bool swap_file_reserve(struct swap_file *swap, size_t new_capacity)
{
    if (!swap) {
        return false;
    }

    return swap_file_grow(swap, new_capacity);
}

/**
 * Write bytes into a swap_file at the specified offset, expanding storage if needed.
 *
 * @param swap Target swap_file to write into; must not be NULL.
 * @param offset Byte offset within the swap_file where writing begins.
 * @param data Pointer to the source buffer to copy from; must not be NULL.
 * @param len Number of bytes to write from `data`.
 * @returns `true` if the data was written and the swap_file resized if necessary, `false` on failure.
 */
bool swap_file_write(struct swap_file *swap, size_t offset, const void *data, size_t len)
{
    if (!swap || !data) {
        return false;
    }

    size_t end = offset + len;
    if (!swap_file_grow(swap, end)) {
        return false;
    }

    memcpy(swap->data + offset, data, len);
    if (end > swap->size) {
        swap->size = end;
    }
    return true;
}

/**
 * Append bytes to the end of a swap_file.
 *
 * @param swap Swap file to append data to.
 * @param data Pointer to the bytes to append.
 * @param len Number of bytes to append from `data`.
 * @returns `true` if the data was appended and the swap file size updated, `false` on failure.
 */
bool swap_file_append(struct swap_file *swap, const void *data, size_t len)
{
    if (!swap) {
        return false;
    }

    return swap_file_write(swap, swap->size, data, len);
}

/**
 * Read bytes from a swap_file into a provided buffer.
 *
 * Copies `len` bytes from `swap` starting at `offset` into `buffer` if the range
 * [offset, offset + len) lies within the stored data.
 *
 * @param swap Swap file to read from.
 * @param offset Byte offset within `swap` where the read begins.
 * @param buffer Destination buffer that will receive the read bytes.
 * @param len Number of bytes to read.
 * @returns `true` if `len` bytes were copied into `buffer`, `false` otherwise.
 */
bool swap_file_read(const struct swap_file *swap, size_t offset, void *buffer, size_t len)
{
    if (!swap || !buffer) {
        return false;
    }

    if (offset + len > swap->size) {
        return false;
    }

    memcpy(buffer, swap->data + offset, len);
    return true;
}

/**
 * Access the raw data buffer of a swap_file.
 *
 * @returns Pointer to the underlying data buffer, or NULL if `swap` is NULL or no buffer is allocated.
 */
const uint8_t *swap_file_data(const struct swap_file *swap)
{
    return swap ? swap->data : 0;
}

/**
 * Initialize a swap_file and populate it with the contents of the file at the given path.
 *
 * @param swap Destination swap_file structure to initialize and fill; on failure its resources
 *             will be released to avoid partial initialization.
 * @param path Filesystem path to read data from.
 * @returns `true` if the swap_file was initialized and its contents set to the file's data
 *          (including when the file is empty), `false` if `swap` or `path` is NULL, the file
 *          cannot be stat'ed or read, or memory allocation fails.
 */
bool swap_file_load_path(struct swap_file *swap, const char *path)
{
    if (!swap || !path) {
        return false;
    }

    struct fs_stat stats;
    if (!fs_stat_path(path, &stats)) {
        return false;
    }

    if (!swap_file_init(swap, stats.size)) {
        return false;
    }

    if (!stats.size) {
        return true;
    }

    if (!swap_file_grow(swap, stats.size)) {
        return false;
    }

    if (!fs_read(path, 0, swap->data, stats.size, &swap->size)) {
        swap_file_free(swap);
        return false;
    }

    swap->size = stats.size;
    return true;
}

/**
 * Flush the in-memory swap contents to a filesystem path, or create the file if empty.
 *
 * If the swap has no allocated data, the target file is touched (created/updated timestamp).
 *
 * @param swap Swap file whose contents will be flushed.
 * @param path Filesystem path to write to or touch.
 * @returns `true` if the file was successfully written or touched; `false` if `swap` or `path` is NULL or the filesystem operation failed.
 */
bool swap_file_flush_path(const struct swap_file *swap, const char *path)
{
    if (!swap || !path) {
        return false;
    }

    if (!swap->data) {
        return fs_touch(path);
    }

    return fs_write(path, 0, swap->data, swap->size, true);
}
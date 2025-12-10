/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: RAM-backed swapfiles that can mirror on-disk data as needed.
 */
#include <lux/swap.h>

#include <lux/fs.h>
#include <lux/memory.h>
#include <string.h>

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

bool swap_file_reserve(struct swap_file *swap, size_t new_capacity)
{
    if (!swap) {
        return false;
    }

    return swap_file_grow(swap, new_capacity);
}

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

bool swap_file_append(struct swap_file *swap, const void *data, size_t len)
{
    if (!swap) {
        return false;
    }

    return swap_file_write(swap, swap->size, data, len);
}

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

const uint8_t *swap_file_data(const struct swap_file *swap)
{
    return swap ? swap->data : 0;
}

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

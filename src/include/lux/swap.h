/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: RAM-backed swapfile helpers for editor workflows.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct swap_file {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

bool swap_file_init(struct swap_file *swap, size_t reserve_bytes);
void swap_file_free(struct swap_file *swap);
bool swap_file_reserve(struct swap_file *swap, size_t new_capacity);
bool swap_file_write(struct swap_file *swap, size_t offset, const void *data, size_t len);
bool swap_file_read(const struct swap_file *swap, size_t offset, void *buffer, size_t len);
bool swap_file_append(struct swap_file *swap, const void *data, size_t len);
const uint8_t *swap_file_data(const struct swap_file *swap);
bool swap_file_load_path(struct swap_file *swap, const char *path);
bool swap_file_flush_path(const struct swap_file *swap, const char *path);

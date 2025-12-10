/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Basic kernel heap interface for dynamic memory allocation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct heap_stats {
	size_t total_bytes;
	size_t used_bytes;
	size_t free_bytes;
	size_t largest_free_block;
	size_t allocation_count;
	size_t free_block_count;
};

void heap_init(void);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
bool heap_get_stats(struct heap_stats *stats);

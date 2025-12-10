/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Basic kernel heap interface for dynamic memory allocation.
 */
#pragma once

#include <stddef.h>

void heap_init(void);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);

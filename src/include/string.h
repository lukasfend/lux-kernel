/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Declarations for freestanding memory and string helpers.
 */
#pragma once

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t len);
void *memmove(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);
int memcmp(const void *lhs, const void *rhs, size_t len);
int strcmp(const char *lhs, const char *rhs);
size_t strlen(const char *str);
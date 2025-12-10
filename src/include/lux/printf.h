/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal printf-style formatting helpers for the kernel.
 */
#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef void (*printf_emit_fn)(char c, void *context);

int kvprintf(printf_emit_fn emit, void *context, const char *fmt, va_list args);
int kprintf(const char *fmt, ...);
int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args);
int snprintf(char *buffer, size_t size, const char *fmt, ...);

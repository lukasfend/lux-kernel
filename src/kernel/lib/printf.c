#include <lux/printf.h>
#include <lux/tty.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

static void emit_char(printf_emit_fn emit, void *context, char c, int *written)
{
    emit(c, context);
    if (written) {
        (*written)++;
    }
}

static void emit_string(printf_emit_fn emit, void *context, const char *str, int *written)
{
    if (!str) {
        str = "(null)";
    }
    while (*str) {
        emit_char(emit, context, *str++, written);
    }
}

static void emit_unsigned_base(printf_emit_fn emit, void *context,
                               unsigned long long value, unsigned base, bool uppercase,
                               int *written)
{
    char buffer[65];
    size_t len = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (base < 2 || base > 16) {
        base = 10;
    }

    if (value == 0) {
        buffer[len++] = '0';
    } else {
        while (value > 0 && len < sizeof(buffer)) {
            buffer[len++] = digits[value % base];
            value /= base;
        }
    }

    while (len--) {
        emit_char(emit, context, buffer[len], written);
    }
}

static void emit_signed(printf_emit_fn emit, void *context, long long value, int *written)
{
    if (value < 0) {
        emit_char(emit, context, '-', written);
        unsigned long long magnitude = (unsigned long long)(-(value + 1)) + 1ULL;
        emit_unsigned_base(emit, context, magnitude, 10, false, written);
    } else {
        emit_unsigned_base(emit, context, (unsigned long long)value, 10, false, written);
    }
}

static unsigned long long fetch_unsigned(va_list *args, int length)
{
    if (length >= 2) {
        return va_arg(*args, unsigned long long);
    }
    if (length == 1) {
        return va_arg(*args, unsigned long);
    }
    return va_arg(*args, unsigned int);
}

static long long fetch_signed(va_list *args, int length)
{
    if (length >= 2) {
        return va_arg(*args, long long);
    }
    if (length == 1) {
        return va_arg(*args, long);
    }
    return va_arg(*args, int);
}

int kvprintf(printf_emit_fn emit, void *context, const char *fmt, va_list args)
{
    if (!emit || !fmt) {
        return 0;
    }

    int written = 0;

    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') {
            emit_char(emit, context, ch, &written);
            continue;
        }

        if (*fmt == '%') {
            ++fmt;
            emit_char(emit, context, '%', &written);
            continue;
        }

        int length = 0;
        while (*fmt == 'l' && length < 2) {
            ++length;
            ++fmt;
        }

        char spec = *fmt ? *fmt++ : '\0';
        if (spec == '\0') {
            emit_char(emit, context, '%', &written);
            break;
        }

        switch (spec) {
        case 'c':
            emit_char(emit, context, (char)va_arg(args, int), &written);
            break;
        case 's':
            emit_string(emit, context, va_arg(args, const char *), &written);
            break;
        case 'd':
        case 'i':
            emit_signed(emit, context, fetch_signed(&args, length), &written);
            break;
        case 'u':
            emit_unsigned_base(emit, context, fetch_unsigned(&args, length), 10, false, &written);
            break;
        case 'x':
            emit_unsigned_base(emit, context, fetch_unsigned(&args, length), 16, false, &written);
            break;
        case 'X':
            emit_unsigned_base(emit, context, fetch_unsigned(&args, length), 16, true, &written);
            break;
        case 'p':
            emit_string(emit, context, "0x", &written);
            emit_unsigned_base(emit, context, (uintptr_t)va_arg(args, void *), 16, false, &written);
            break;
        default:
            emit_char(emit, context, '%', &written);
            emit_char(emit, context, spec, &written);
            break;
        }
    }

    return written;
}

struct buffer_ctx {
    char *buffer;
    size_t size;
    size_t pos;
};

static void buffer_emit(char c, void *context)
{
    struct buffer_ctx *buf = context;
    if (!buf->buffer || buf->size == 0) {
        buf->pos++;
        return;
    }

    if (buf->pos + 1 < buf->size) {
        buf->buffer[buf->pos] = c;
    }
    buf->pos++;
}

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args)
{
    struct buffer_ctx ctx = {
        .buffer = buffer,
        .size = size,
        .pos = 0
    };

    va_list copy;
    va_copy(copy, args);
    kvprintf(buffer_emit, &ctx, fmt, copy);
    va_end(copy);

    if (size > 0 && buffer) {
        size_t index = (ctx.pos < size - 1) ? ctx.pos : size - 1;
        buffer[index] = '\0';
    }

    return (int)ctx.pos;
}

int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

static void tty_emit(char c, void *context)
{
    (void)context;
    tty_putc(c);
}

int kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = kvprintf(tty_emit, NULL, fmt, args);
    va_end(args);
    return written;
}

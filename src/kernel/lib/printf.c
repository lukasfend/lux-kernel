#include <lux/printf.h>
#include <lux/tty.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/**
 * Invoke the provided emit function to output a single character and, if supplied,
 * increment a caller-provided written-character counter.
 *
 * @param emit Function called to emit the character; it receives the character and context.
 * @param context Opaque pointer passed through to `emit`.
 * @param c Character to be emitted.
 * @param written If non-NULL, pointer to an integer that will be incremented by one to reflect the emitted character.
 */
static void emit_char(printf_emit_fn emit, void *context, char c, int *written)
{
    emit(c, context);
    if (written) {
        (*written)++;
    }
}

/**
 * Emit a null-terminated C string by calling the provided emit function for each character.
 *
 * If `str` is NULL, the literal "(null)" is emitted. If `written` is non-NULL, the integer
 * it points to is incremented once per character emitted.
 *
 * @param emit   Function used to emit each output character.
 * @param context Opaque context pointer passed through to `emit`.
 * @param str    Null-terminated string to emit; may be NULL.
 * @param written Optional pointer to an integer that will be incremented for each emitted character; may be NULL.
 */
static void emit_string(printf_emit_fn emit, void *context, const char *str, int *written)
{
    if (!str) {
        str = "(null)";
    }
    while (*str) {
        emit_char(emit, context, *str++, written);
    }
}

/**
 * Emit an unsigned integer in the specified base using the provided emit function.
 *
 * Emits the textual representation of `value` in `base` (clamped to 10 if outside 2–16),
 * uses uppercase hexadecimal digits when `uppercase` is true, and emits "0" when `value` is 0.
 *
 * @param emit Emit callback used to output each character.
 * @param context Opaque context pointer forwarded to `emit`.
 * @param value Unsigned integer value to emit.
 * @param base Numeric base to use for conversion; values less than 2 or greater than 16 are treated as 10.
 * @param uppercase If non-zero, use uppercase letters for bases > 10.
 * @param written Optional pointer to an integer that will be incremented for each emitted character; may be NULL.
 */
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

/**
 * Emit a signed integer in decimal form to the provided emit function.
 *
 * Emits a leading '-' for negative values and then the absolute magnitude in base 10.
 * Correctly handles the most-negative two's‑complement value without overflow.
 *
 * @param emit Function used to output individual characters.
 * @param context Opaque context passed to the emit function.
 * @param value Signed integer value to emit.
 * @param written If non-NULL, receives incremental count of characters emitted.
 */
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

/**
 * Retrieve an unsigned integer argument from a va_list according to a length modifier.
 *
 * @param args Pointer to the variadic argument list to read from.
 * @param length Length modifier: 0 for `unsigned int`, 1 for `unsigned long`, 2 or greater for `unsigned long long`.
 * @returns The next argument cast to an unsigned integer type based on `length`.
 */
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

/**
 * Retrieve the next signed integer argument from a va_list according to length.
 *
 * @param args Pointer to the va_list to read the argument from.
 * @param length Number of 'l' length modifiers seen: 0 => `int`, 1 => `long`, >=2 => `long long`.
 * @return The fetched signed integer promoted to `long long`.
 */
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

/**
 * Format and emit a string according to a subset of printf-style specifiers.
 *
 * Processes the format string and calls the provided emit function for each
 * resulting character. Supports "%%", length modifier "l" (once or twice) and
 * the specifiers: %c, %s, %d, %i, %u, %x, %X, and %p. Unknown specifiers are
 * emitted as '%' followed by the specifier character.
 *
 * @param emit Function called for every output character.
 * @param context Opaque pointer passed through to `emit`.
 * @param fmt Null-terminated printf-style format string.
 * @param args Variadic argument list supplying values for format specifiers.
 * @returns The total number of characters emitted.
 */
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

/**
 * Emit a character into a buffer context and advance the position.
 *
 * Writes `c` into the buffer at the current position when a destination buffer
 * is provided and there is room for at least one more character; otherwise
 * only advances the recorded position to reflect the attempted write.
 *
 * @param c Character to emit.
 * @param context Pointer to a `struct buffer_ctx` containing `buffer`, `size`,
 *                and `pos`. */
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

/**
 * Format a string into a provided buffer using a va_list while respecting the buffer size limit.
 *
 * The destination buffer is always null-terminated when size > 0; if size is 0 no bytes are written.
 * The function does not report formatting errors and will compute the full length that would have
 * been produced regardless of whether it fit into the buffer.
 *
 * @param buffer Destination buffer for the formatted string (may be NULL if size is 0).
 * @param size Maximum number of bytes to write to buffer, including the terminating null byte.
 * @param fmt Format string.
 * @param args Argument list for the format string (va_list).
 * @returns The number of characters that would have been written, excluding the terminating null byte.
 */
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

/**
 * Format a null-terminated string into a user-supplied buffer with size limit.
 *
 * Formats according to `fmt` and writes the result into `buffer`. If `size` is greater than zero,
 * the resulting buffer is guaranteed to be null-terminated (truncated if necessary).
 *
 * @param buffer Destination buffer for the formatted string; may be NULL when `size` is zero.
 * @param size   Size of `buffer` in bytes.
 * @param fmt    printf-style format string.
 * @returns The number of characters that would have been written, excluding the terminating
 *          null byte. If the return value is greater than or equal to `size`, the output was truncated.
 */
int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

/**
 * Emit a character to the kernel TTY.
 *
 * Sends the character `c` to the system TTY output; the `context` parameter is ignored.
 *
 * @param c Character to emit.
 * @param context Unused; present to match the emit callback signature.
 */
static void tty_emit(char c, void *context)
{
    (void)context;
    tty_putc(c);
}

/**
 * Format a string according to fmt and write the result to the kernel TTY.
 *
 * @param fmt printf-style format string.
 * @return The number of characters written to the TTY.
 */
int kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = kvprintf(tty_emit, NULL, fmt, args);
    va_end(args);
    return written;
}
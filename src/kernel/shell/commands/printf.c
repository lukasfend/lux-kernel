#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>

static const char *usage_message = "Usage: printf <format> [args...]\n";

/**
 * Convert a hexadecimal digit character to its numeric value.
 *
 * @param c Hexadecimal digit character to decode.
 * @returns `0` through `15` for characters '0'–'9', 'a'–'f', or 'A'–'F'; `-1` if `c` is not a hexadecimal digit.
 */
static int digit_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

/**
 * Parse a NUL-terminated string as an unsigned integer in the specified base.
 *
 * @param str String containing the digits to parse; must be non-NULL and non-empty.
 * @param base Radix to use for conversion (expected range: 2 to 16).
 * @param out Output pointer where the parsed unsigned long long value is stored on success.
 * @returns `true` if the string was successfully parsed and `*out` set, `false` if `str` is NULL/empty
 *          or contains a character that is not a valid digit for the given base.
 */
static bool parse_unsigned_base(const char *str, unsigned base, unsigned long long *out)
{
    if (!str || !*str) {
        return false;
    }

    unsigned long long value = 0;
    int digit;
    while (*str) {
        digit = digit_value(*str++);
        if (digit < 0 || (unsigned)digit >= base) {
            return false;
        }
        value = value * base + (unsigned long long)digit;
    }

    *out = value;
    return true;
}

/**
 * Parse a numeric string as an unsigned integer, automatically using base 16 if it has a `0x`/`0X` prefix or base 10 otherwise.
 *
 * If `str` begins with `0x` or `0X` followed by at least one character, the remainder is parsed as hexadecimal; otherwise the whole string is parsed as decimal.
 * @param str Null-terminated string containing the numeric representation to parse.
 * @param out Pointer to an unsigned long long where the parsed value will be stored on success.
 * @returns `true` if parsing succeeded and `*out` was set to the parsed value, `false` otherwise.
 */
static bool parse_unsigned_auto(const char *str, unsigned long long *out)
{
    if (!str || !*str) {
        return false;
    }

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') && str[2] != '\0') {
        return parse_unsigned_base(str + 2, 16, out);
    }
    return parse_unsigned_base(str, 10, out);
}

/**
 * Parse a hexadecimal unsigned integer from the given string, accepting an optional "0x" or "0X" prefix.
 * @param str Null-terminated input string containing the hexadecimal representation (may start with "0x" or "0X").
 * @param out Pointer where the parsed unsigned long long value will be stored on success.
 * @returns `true` if parsing succeeds and `*out` contains the parsed value, `false` otherwise (e.g., `str` is NULL or contains invalid digits).
 */
static bool parse_unsigned_hex(const char *str, unsigned long long *out)
{
    if (!str) {
        return false;
    }
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') && str[2] != '\0') {
        str += 2;
    }
    return parse_unsigned_base(str, 16, out);
}

/**
 * Parse a signed integer from a string, accepting an optional '+' or '-' sign and decimal or hexadecimal
 * numeric notation (hex accepts a "0x" or "0X" prefix).
 * @param str Input string containing the signed numeric representation.
 * @param out Pointer to a variable that receives the parsed signed value on success.
 * @returns `true` if the string contains a valid signed integer and `*out` was set, `false` otherwise.
 */
static bool parse_signed(const char *str, long long *out)
{
    if (!str || !*str) {
        return false;
    }

    bool negative = false;
    if (*str == '-') {
        negative = true;
        ++str;
    } else if (*str == '+') {
        ++str;
    }

    if (!*str) {
        return false;
    }

    unsigned long long magnitude;
    if (!parse_unsigned_auto(str, &magnitude)) {
        return false;
    }

    if (negative) {
        *out = -(long long)magnitude;
    } else {
        *out = (long long)magnitude;
    }
    return true;
}

/**
 * Write an unsigned integer to the provided shell I/O in the specified base.
 *
 * Converts `value` to its textual representation using `base` and writes the
 * digits to `io` in the correct order. If `uppercase` is true, alphabetic
 * digits (for bases > 10) are written as uppercase letters.
 *
 * @param value Unsigned integer to write.
 * @param base Numeric base to use for conversion; values outside 2–16 cause
 *             decimal (base 10) to be used.
 * @param uppercase If true, use `A`–`F` for hex digits; otherwise use `a`–`f`.
 */
static void write_unsigned(const struct shell_io *io, unsigned long long value, unsigned base, bool uppercase)
{
    static const char *digits_lower = "0123456789abcdef";
    static const char *digits_upper = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;

    char buffer[65];
    size_t len = 0;

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
        shell_io_putc(io, buffer[len]);
    }
}

/**
 * Write a signed decimal representation of value to the provided shell I/O.
 *
 * The output is the decimal digits of the absolute value, prefixed with '-'
 * if value is negative.
 *
 * @param io   Output interface to receive characters.
 * @param value Signed integer value to format and write.
 */
static void write_signed(const struct shell_io *io, long long value)
{
    if (value < 0) {
        shell_io_putc(io, '-');
        unsigned long long magnitude = (unsigned long long)(-(value + 1)) + 1ULL;
        write_unsigned(io, magnitude, 10, false);
    } else {
        write_unsigned(io, (unsigned long long)value, 10, false);
    }
}

/**
 * Write a pointer value to the shell IO as a hexadecimal literal prefixed with "0x".
 *
 * @param value Pointer value to format and write (printed in lowercase hexadecimal).
 */
static void write_pointer(const struct shell_io *io, uintptr_t value)
{
    shell_io_write_string(io, "0x");
    write_unsigned(io, (unsigned long long)value, 16, false);
}

/**
 * Report a missing argument for a printf format specifier to the shell I/O.
 *
 * @param spec The format specifier character that is missing its corresponding argument.
 */
static void report_missing_argument(const struct shell_io *io, char spec)
{
    shell_io_write_string(io, "printf: missing argument for %");
    shell_io_putc(io, spec);
    shell_io_putc(io, '\n');
}

/**
 * Report an invalid numeric argument by writing an error message to the provided shell IO.
 * @param arg The offending argument string; it is included verbatim in the printed message.
 */
static void report_invalid_argument(const struct shell_io *io, const char *arg)
{
    shell_io_write_string(io, "printf: invalid numeric argument '");
    shell_io_write_string(io, arg);
    shell_io_write_string(io, "'\n");
}

/**
 * Decode a single-character escape sequence to its interpreted character.
 *
 * @param c Escape sequence character (e.g., 'n' for newline, 't' for tab).
 * @returns The interpreted character: newline for 'n', carriage return for 'r',
 *          tab for 't', backslash for '\', double quote for '"', NUL for '0';
 *          returns `c` unchanged if it is not a recognized escape code.
 */
static char decode_escape(char c)
{
    switch (c) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        return c;
    }
}

/**
 * Handle the "printf" shell command by formatting and writing output according to
 * a format string and the provided arguments.
 *
 * Supported format specifiers: %s (string), %c (character), %d / %i (signed decimal),
 * %u (unsigned decimal), %x / %X (hexadecimal), %p (pointer as hex), and %% (literal '%').
 * Backslash escape sequences such as \n, \r, \t, \\, \", and \0 are recognized.
 *
 * If argc < 2 the usage message is written. For a format specifier that requires an
 * argument, a missing argument triggers an error report and the handler returns.
 * Numeric conversion failures report an invalid-argument error and cause the handler
 * to return.
 *
 * @param argc Number of command arguments (including the command name).
 * @param argv Argument vector; argv[1] is treated as the format string and subsequent
 *             entries provide values for format specifiers.
 * @param io   Shell I/O interface used for all output and error reporting.
 */
static void printf_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2) {
        shell_io_write_string(io, usage_message);
        return;
    }

    const char *fmt = argv[1];
    int next_arg = 2;

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        char c = fmt[i];

        if (c == '\\') {
            char next = fmt[i + 1];
            if (next != '\0') {
                ++i;
                shell_io_putc(io, decode_escape(next));
                continue;
            }
        }

        if (c != '%') {
            shell_io_putc(io, c);
            continue;
        }

        char spec = fmt[++i];
        if (spec == '\0') {
            shell_io_putc(io, '%');
            break;
        }

        if (spec == '%') {
            shell_io_putc(io, '%');
            continue;
        }

        if (next_arg >= argc) {
            report_missing_argument(io, spec);
            return;
        }

        const char *arg = argv[next_arg++];
        switch (spec) {
        case 's':
            shell_io_write_string(io, arg);
            break;
        case 'c':
            shell_io_putc(io, arg[0]);
            break;
        case 'd':
        case 'i': {
            long long value;
            if (!parse_signed(arg, &value)) {
                report_invalid_argument(io, arg);
                return;
            }
            write_signed(io, value);
            break;
        }
        case 'u': {
            unsigned long long value;
            if (!parse_unsigned_auto(arg, &value)) {
                report_invalid_argument(io, arg);
                return;
            }
            write_unsigned(io, value, 10, false);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long value;
            if (!parse_unsigned_hex(arg, &value)) {
                report_invalid_argument(io, arg);
                return;
            }
            write_unsigned(io, value, 16, spec == 'X');
            break;
        }
        case 'p': {
            unsigned long long value;
            if (!parse_unsigned_hex(arg, &value)) {
                report_invalid_argument(io, arg);
                return;
            }
            write_pointer(io, (uintptr_t)value);
            break;
        }
        default:
            shell_io_putc(io, '%');
            shell_io_putc(io, spec);
            break;
        }
    }
}

const struct shell_command shell_command_printf = {
    .name = "printf",
    .help = "Prints a string with the given formatting",
    .handler = printf_handler
};
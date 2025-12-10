#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>

static const char *usage_message = "Usage: printf <format> [args...]\n";

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

static void write_pointer(const struct shell_io *io, uintptr_t value)
{
    shell_io_write_string(io, "0x");
    write_unsigned(io, (unsigned long long)value, 16, false);
}

static void report_missing_argument(const struct shell_io *io, char spec)
{
    shell_io_write_string(io, "printf: missing argument for %");
    shell_io_putc(io, spec);
    shell_io_putc(io, '\n');
}

static void report_invalid_argument(const struct shell_io *io, const char *arg)
{
    shell_io_write_string(io, "printf: invalid numeric argument '");
    shell_io_write_string(io, arg);
    shell_io_write_string(io, "'\n");
}

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
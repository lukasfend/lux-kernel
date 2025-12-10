#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>

#define HEXDUMP_BYTES_PER_LINE 16u
#define HEXDUMP_MAX_BYTES 512u

static char hex_digit(uint8_t value)
{
    if (value < 10u) {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10u));
}

static void io_write_hex32(const struct shell_io *io, uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xFu);
        shell_io_putc(io, hex_digit(nibble));
    }
}

static void io_write_hex8(const struct shell_io *io, uint8_t value)
{
    shell_io_putc(io, hex_digit((uint8_t)(value >> 4)));
    shell_io_putc(io, hex_digit((uint8_t)(value & 0xFu)));
}

static bool parse_unsigned(const char *text, size_t *out)
{
    if (!text || !out || !*text) {
        return false;
    }

    unsigned base = 10u;
    size_t index = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16u;
        index = 2u;
    }

    size_t value = 0;
    for (; text[index]; ++index) {
        char c = text[index];
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = (unsigned)(c - '0');
        } else if (base == 16u && c >= 'a' && c <= 'f') {
            digit = 10u + (unsigned)(c - 'a');
        } else if (base == 16u && c >= 'A' && c <= 'F') {
            digit = 10u + (unsigned)(c - 'A');
        } else {
            return false;
        }
        value = value * base + digit;
    }

    *out = value;
    return true;
}

static void write_ascii(const struct shell_io *io, const uint8_t *data, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        char c = (char)data[i];
        if (c < 32 || c > 126) {
            shell_io_putc(io, '.');
        } else {
            shell_io_putc(io, c);
        }
    }
}

static void hexdump_region(const struct shell_io *io, const uint8_t *base, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        size_t line_count = length - offset;
        if (line_count > HEXDUMP_BYTES_PER_LINE) {
            line_count = HEXDUMP_BYTES_PER_LINE;
        }

        io_write_hex32(io, (uint32_t)((uintptr_t)base + offset));
        shell_io_write_string(io, ": ");

        for (size_t i = 0; i < HEXDUMP_BYTES_PER_LINE; ++i) {
            if (i < line_count) {
                io_write_hex8(io, base[offset + i]);
            } else {
                shell_io_write_string(io, "  ");
            }
            shell_io_putc(io, ' ');
        }

        shell_io_putc(io, ' ');
        write_ascii(io, &base[offset], line_count);
        shell_io_putc(io, '\n');

        offset += line_count;
    }
}

static void hexdump_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2 || argc > 3) {
        shell_io_write_string(io, "Usage: hexdump <address> [length]\n");
        return;
    }

    size_t address = 0;
    size_t length = 128u;

    if (!parse_unsigned(argv[1], &address)) {
        shell_io_write_string(io, "Invalid address. Use decimal or 0x-prefixed hex.\n");
        return;
    }

    if (argc == 3 && !parse_unsigned(argv[2], &length)) {
        shell_io_write_string(io, "Invalid length. Use decimal or 0x-prefixed hex.\n");
        return;
    }

    if (!length) {
        shell_io_write_string(io, "Length must be greater than zero.\n");
        return;
    }

    if (length > HEXDUMP_MAX_BYTES) {
        length = HEXDUMP_MAX_BYTES;
    }

    shell_io_write_string(io, "Dumping ");
    char length_msg[16];
    size_t tmp = length;
    size_t idx = 0;
    if (tmp == 0) {
        length_msg[idx++] = '0';
    } else {
        while (tmp && idx < sizeof(length_msg)) {
            length_msg[idx++] = (char)('0' + (tmp % 10u));
            tmp /= 10u;
        }
    }
    while (idx) {
        shell_io_putc(io, length_msg[--idx]);
    }
    shell_io_write_string(io, " bytes from 0x");
    io_write_hex32(io, (uint32_t)address);
    shell_io_putc(io, '\n');

    hexdump_region(io, (const uint8_t *)address, length);
}

const struct shell_command shell_command_hexdump = {
    .name = "hexdump",
    .help = "Hexdump memory: hexdump <addr> [len]",
    .handler = hexdump_handler,
};

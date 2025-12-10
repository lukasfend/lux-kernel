#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lux/shell.h>

#define HEXDUMP_BYTES_PER_LINE 16u
#define HEXDUMP_MAX_BYTES 512u

/**
 * Convert a 4-bit value to its uppercase hexadecimal digit.
 *
 * @param value Value in the range 0-15 to convert to a hex digit.
 * @return Character '0'-'9' or 'A'-'F' corresponding to the value.
 */
static char hex_digit(uint8_t value)
{
    if (value < 10u) {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10u));
}

/**
 * Write a 32-bit value as eight hexadecimal characters to the provided shell IO.
 *
 * @param value 32-bit unsigned value to write as eight hex digits (most significant nibble first).
 */
static void io_write_hex32(const struct shell_io *io, uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xFu);
        shell_io_putc(io, hex_digit(nibble));
    }
}

/**
 * Write a single byte to the provided shell IO as two hexadecimal characters.
 *
 * @param io   Shell IO target where the hex digits will be written.
 * @param value Byte value to format and write as two uppercase hex digits (high nibble first).
 */
static void io_write_hex8(const struct shell_io *io, uint8_t value)
{
    shell_io_putc(io, hex_digit((uint8_t)(value >> 4)));
    shell_io_putc(io, hex_digit((uint8_t)(value & 0xFu)));
}

/**
 * Parse a non-negative integer from a string in decimal or 0x/0X-prefixed hexadecimal form.
 *
 * Accepts a non-null, non-empty `text` string containing either decimal digits or hexadecimal
 * digits when prefixed with `0x` or `0X`. On success stores the parsed value in `*out`.
 *
 * @param text Null-terminated input string representing the unsigned integer (decimal or `0x` hex).
 * @param out Pointer to receive the parsed value.
 * @returns `true` if `text` was a valid unsigned integer and the value was stored in `*out`, `false` otherwise.
 */
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

/**
 * Write a sequence of bytes to the shell IO as ASCII characters, substituting '.'
 * for bytes outside the printable ASCII range (32 through 126).
 *
 * @param io   Shell IO interface to write characters to.
 * @param data Pointer to the byte buffer to render.
 * @param count Number of bytes to write from `data`.
 */
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

/**
 * Dump a memory region to the shell IO as hexadecimal bytes with an ASCII side column.
 *
 * Each output line begins with the absolute address (base + offset) in hex followed by
 * up to HEXDUMP_BYTES_PER_LINE bytes rendered as two-digit hex values (space-padded if
 * the final line is short) and an ASCII representation where non-printable bytes are
 * shown as `.`.
 *
 * @param io   Shell IO to write the dump to.
 * @param base Pointer to the start of the memory region to dump.
 * @param length Number of bytes to dump from `base`.
 */
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

/**
 * Handle the "hexdump" shell command: parse an address and optional length,
 * validate and clamp the length, print a header, and produce a hex+ASCII dump
 * of memory starting at the given address.
 *
 * @param argc Number of command arguments (including command name).
 * @param argv Argument vector where argv[1] is the address and argv[2] (optional) is the length; both accept decimal or `0x`-prefixed hex.
 * @param io   Shell I/O interface used to emit messages and dump output.
 */
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
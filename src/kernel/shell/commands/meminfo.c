#include <lux/memory.h>
#include <lux/shell.h>

/**
 * Write a non-negative integer value to the shell IO as ASCII decimal digits.
 *
 * @param io Shell IO to receive the output characters.
 * @param value Integer value to format and write in base 10.
 */
static void io_write_dec(const struct shell_io *io, size_t value)
{
    char buffer[32];
    size_t index = 0;

    if (value == 0) {
        shell_io_putc(io, '0');
        return;
    }

    while (value && index < sizeof(buffer)) {
        buffer[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (index) {
        shell_io_putc(io, buffer[--index]);
    }
}

/**
 * Write a labeled numeric line to the shell IO.
 *
 * Writes the provided label, the decimal representation of value, an optional
 * suffix if non-NULL, and a trailing newline to the given shell IO.
 *
 * @param io     Target shell IO to write to.
 * @param label  String label printed before the numeric value.
 * @param value  Numeric value printed in decimal.
 * @param suffix Optional string printed after the numeric value; may be NULL.
 */
static void io_write_line(const struct shell_io *io, const char *label, size_t value, const char *suffix)
{
    shell_io_write_string(io, label);
    io_write_dec(io, value);
    if (suffix) {
        shell_io_write_string(io, suffix);
    }
    shell_io_putc(io, '\n');
}

/**
 * Handle the `meminfo` shell command by printing kernel heap statistics to the given shell I/O.
 *
 * Queries the kernel heap statistics and writes a human-readable summary (total, used, free,
 * largest free block, allocation count, and free block count). If statistics cannot be retrieved,
 * an error message is written instead.
 *
 * @param io Shell I/O to which the output is written.
 */
static void meminfo_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    struct heap_stats stats;
    if (!heap_get_stats(&stats)) {
        shell_io_write_string(io, "Unable to query heap statistics.\n");
        return;
    }

    shell_io_write_string(io, "Kernel heap usage:\n");
    io_write_line(io, "  Total: ", stats.total_bytes, " bytes");
    io_write_line(io, "  Used : ", stats.used_bytes, " bytes");
    io_write_line(io, "  Free : ", stats.free_bytes, " bytes");
    io_write_line(io, "  Largest free block: ", stats.largest_free_block, " bytes");
    io_write_line(io, "  Allocations: ", stats.allocation_count, 0);
    io_write_line(io, "  Free blocks: ", stats.free_block_count, 0);
}

const struct shell_command shell_command_meminfo = {
    .name = "meminfo",
    .help = "Show kernel heap statistics",
    .handler = meminfo_handler,
};
#include <lux/memory.h>
#include <lux/shell.h>

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

static void io_write_line(const struct shell_io *io, const char *label, size_t value, const char *suffix)
{
    shell_io_write_string(io, label);
    io_write_dec(io, value);
    if (suffix) {
        shell_io_write_string(io, suffix);
    }
    shell_io_putc(io, '\n');
}

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

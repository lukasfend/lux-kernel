#include <lux/memory.h>
#include <lux/shell.h>
#include <lux/tty.h>

static void tty_write_dec(size_t value)
{
    char buffer[32];
    size_t index = 0;

    if (value == 0) {
        tty_putc('0');
        return;
    }

    while (value && index < sizeof(buffer)) {
        buffer[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (index) {
        tty_putc(buffer[--index]);
    }
}

static void tty_write_line(const char *label, size_t value, const char *suffix)
{
    tty_write_string(label);
    tty_write_dec(value);
    if (suffix) {
        tty_write_string(suffix);
    }
    tty_putc('\n');
}

static void meminfo_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct heap_stats stats;
    if (!heap_get_stats(&stats)) {
        tty_write_string("Unable to query heap statistics.\n");
        return;
    }

    tty_write_string("Kernel heap usage:\n");
    tty_write_line("  Total: ", stats.total_bytes, " bytes");
    tty_write_line("  Used : ", stats.used_bytes, " bytes");
    tty_write_line("  Free : ", stats.free_bytes, " bytes");
    tty_write_line("  Largest free block: ", stats.largest_free_block, " bytes");
    tty_write_line("  Allocations: ", stats.allocation_count, 0);
    tty_write_line("  Free blocks: ", stats.free_block_count, 0);
}

const struct shell_command shell_command_meminfo = {
    .name = "meminfo",
    .help = "Show kernel heap statistics",
    .handler = meminfo_handler,
};

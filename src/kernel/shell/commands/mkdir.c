#include <lux/fs.h>
#include <lux/shell.h>

/**
 * Print usage information for the mkdir command.
 */
static void mkdir_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: mkdir <path> [path...]\n");
}

/**
 * Emit a formatted mkdir error message.
 */
static void mkdir_print_error(const struct shell_io *io, const char *path, const char *reason)
{
    shell_io_write_string(io, "mkdir: ");
    shell_io_write_string(io, path);
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

/**
 * Handle the mkdir command by creating each requested directory.
 */
static void mkdir_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2) {
        mkdir_usage(io);
        return;
    }

    if (!fs_ready()) {
        shell_io_write_string(io, "mkdir: filesystem not available\n");
        return;
    }

    for (int i = 1; i < argc; ++i) {
        const char *path = argv[i];
        struct fs_stat stats;
        if (fs_stat_path(path, &stats)) {
            mkdir_print_error(io, path, "already exists");
            continue;
        }

        if (!fs_mkdir(path)) {
            mkdir_print_error(io, path, "cannot create directory");
        }
    }
}

const struct shell_command shell_command_mkdir = {
    .name = "mkdir",
    .help = "Creates a new directory",
    .handler = mkdir_handler,
};
#include <lux/fs.h>
#include <lux/shell.h>

/**
 * Show the usage message for the mkdir command on the provided shell I/O.
 * @param io I/O stream to which the usage string is written.
 */
static void mkdir_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: mkdir <path> [path...]\n");
}

/**
 * Print a formatted mkdir error for a specific path to the provided shell I/O.
 *
 * @param path Path that caused the error.
 * @param reason Human-readable reason for the failure (for example, "already exists").
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
 * Create directories for each path argument of the mkdir shell command.
 *
 * If invoked with fewer than one path argument it prints usage. If the
 * filesystem is unavailable it reports that error. For each provided path,
 * existing paths produce an "already exists" error and creation failures
 * produce a "cannot create directory" error; successfully created directories
 * produce no output.
 *
 * @param argc Number of arguments (including command name).
 * @param argv Argument vector where argv[1..argc-1] are target paths.
 * @param io Shell I/O used for usage and error messages.
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
        char resolved[SHELL_PATH_MAX];
        if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
            mkdir_print_error(io, path, "path too long");
            continue;
        }

        struct fs_stat stats;
        if (fs_stat_path(resolved, &stats)) {
            mkdir_print_error(io, path, "already exists");
            continue;
        }

        if (!fs_mkdir(resolved)) {
            mkdir_print_error(io, path, "cannot create directory");
        }
    }
}

const struct shell_command shell_command_mkdir = {
    .name = "mkdir",
    .help = "Creates a new directory",
    .handler = mkdir_handler,
};
#include <lux/fs.h>
#include <lux/shell.h>

/**
 * Print the usage message for the `touch` command to the given shell IO.
 *
 * @param io Shell IO to write the usage text to.
 */
static void touch_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: touch <path> [path...]\n");
    shell_io_write_string(io, "Pipe data into touch to overwrite a single file.\n");
}

/**
 * Write a formatted error message for the touch command to the provided shell IO.
 *
 * @param path Path of the file related to the error.
 * @param reason Short text describing the error cause.
 */
static void touch_print_error(const struct shell_io *io, const char *path, const char *reason)
{
    shell_io_write_string(io, "touch: ");
    shell_io_write_string(io, path);
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

/**
 * Handle the shell "touch" command: create files and optionally write piped data.
 *
 * Prints usage when no path arguments are provided and prints an error if the
 * filesystem is not available. If piped input is present it must target a
 * single path; otherwise an error is printed. For each provided path this
 * function attempts to create the file and, when piped data exists, overwrites
 * the file contents with that data.
 *
 * @param argc Number of arguments (including command name).
 * @param argv Argument vector; target paths are listed in argv[1]..argv[argc-1].
 * @param io Shell I/O context; may contain piped input in io->input with length
 *           io->input_len.
 */
static void touch_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2) {
        touch_usage(io);
        return;
    }

    if (!fs_ready()) {
        shell_io_write_string(io, "touch: filesystem not available\n");
        return;
    }

    const char *pipe_data = (io && io->input_len) ? io->input : 0;
    size_t pipe_len = (io && io->input_len) ? io->input_len : 0;

    if (pipe_len && argc != 2) {
        shell_io_write_string(io, "touch: piped data requires a single target\n");
        return;
    }

    for (int i = 1; i < argc; ++i) {
        const char *path = argv[i];
        char resolved[SHELL_PATH_MAX];
        if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
            touch_print_error(io, path, "path too long");
            continue;
        }

        if (!fs_touch(resolved)) {
            touch_print_error(io, path, "cannot create file");
            continue;
        }

        if (pipe_len) {
            if (!fs_write(resolved, 0, pipe_data, pipe_len, true)) {
                touch_print_error(io, path, "write failed");
            }
        }
    }
}

const struct shell_command shell_command_touch = {
    .name = "touch",
    .help = "Create files (writes piped data if provided)",
    .handler = touch_handler,
};
#include <lux/fs.h>
#include <lux/shell.h>

#define CAT_BUFFER_SIZE 512u

/**
 * Write the cat command usage string to the provided shell IO.
 */
static void cat_print_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: cat <path> [path...]\n");
}

/**
 * Write a standardized cat error message to the given shell IO.
 *
 * Format: "cat: <path>: <reason>\n".
 *
 * @param io   Shell IO to write the message to.
 * @param path Filesystem path associated with the error.
 * @param reason Short description of the error reason.
 */
static void cat_print_error(const struct shell_io *io, const char *path, const char *reason)
{
    shell_io_write_string(io, "cat: ");
    shell_io_write_string(io, path);
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

/**
 * Stream the contents of a regular file to the provided shell IO.
 *
 * @param path Filesystem path of the file to read.
 * @param io   Shell IO to which file data and error messages are written.
 * @returns `true` if the file was successfully streamed to `io`, `false` if the path was not found,
 *          referred to a directory, or a read error occurred.
 */
static bool cat_stream_file(const char *path, const struct shell_io *io)
{
    char resolved[SHELL_PATH_MAX];
    if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
        cat_print_error(io, path, "path too long");
        return false;
    }

    struct fs_stat stats;
    if (!fs_stat_path(resolved, &stats)) {
        cat_print_error(io, path, "not found");
        return false;
    }
    if (stats.is_dir) {
        cat_print_error(io, path, "is a directory");
        return false;
    }

    size_t offset = 0;
    char buffer[CAT_BUFFER_SIZE];

    while (offset < stats.size) {
        size_t chunk = stats.size - offset;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        size_t bytes_read = 0;
        if (!fs_read(resolved, offset, buffer, chunk, &bytes_read)) {
            cat_print_error(io, path, "read error");
            return false;
        }

        if (!bytes_read) {
            break;
        }

        shell_io_write(io, buffer, bytes_read);
        offset += bytes_read;
    }

    return true;
}

/**
 * Handle the `cat` shell command: print file contents or echo provided shell input.
 *
 * When one or more file paths are given, streams each file's contents to the provided shell I/O.
 * If no path is provided and the shell I/O contains input data, writes that input back to the I/O.
 * If no path is provided and there is no input, writes the command usage. If the filesystem is
 * unavailable, writes a filesystem-unavailable error to the I/O.
 *
 * @param argc Number of arguments in `argv` (command name plus any paths).
 * @param argv Argument vector where argv[1..argc-1] are file paths to display.
 * @param io   Shell I/O to read input from and write output/errors to; may be NULL.
 */
static void cat_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2) {
        if (io && io->input && io->input_len) {
            shell_io_write(io, io->input, io->input_len);
            return;
        }
        cat_print_usage(io);
        return;
    }

    if (!fs_ready()) {
        shell_io_write_string(io, "cat: filesystem not available\n");
        return;
    }

    for (int i = 1; i < argc; ++i) {
        cat_stream_file(argv[i], io);
    }
}

const struct shell_command shell_command_cat = {
    .name = "cat",
    .help = "Display file contents",
    .handler = cat_handler,
};
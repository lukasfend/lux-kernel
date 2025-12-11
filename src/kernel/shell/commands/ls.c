#include <lux/fs.h>
#include <lux/printf.h>
#include <lux/shell.h>

struct ls_ctx {
    const struct shell_io *io;
};

/**
 * Emit a single directory entry line to the configured shell I/O.
 *
 * Formats and writes a line containing an entry type indicator (`d` for directory,
 * `-` for file), the entry size in bytes, and the entry name, followed by a newline.
 *
 * @param entry Pointer to the directory entry to emit.
 * @param user_data Pointer to an `ls_ctx` instance carrying the `shell_io` to write to;
 *                  if `user_data` is NULL or its `io` is NULL, the function does nothing.
 */
static void ls_emit(const struct fs_dirent *entry, void *user_data)
{
    struct ls_ctx *ctx = (struct ls_ctx *)user_data;
    if (!ctx || !ctx->io) {
        return;
    }

    char line[96];
    snprintf(line, sizeof(line), "%c %lu %s\n",
             entry->is_dir ? 'd' : '-',
             (unsigned long)entry->size,
             entry->name);
    shell_io_write_string(ctx->io, line);
}

/**
 * Print a standardized "cannot access" error message to the provided shell I/O.
 *
 * If `path` is NULL, "/" is used in the message.
 * @param io Shell I/O interface to write the message to.
 * @param path Path that could not be accessed, or NULL to indicate root ("/").
 */
static void ls_print_error(const struct shell_io *io, const char *path)
{
    shell_io_write_string(io, "ls: cannot access ");
    shell_io_write_string(io, path ? path : "/");
    shell_io_write_string(io, "\n");
}

/**
 * List directory contents for a given path and write formatted output to the provided shell I/O.
 *
 * If `path` is NULL, the root path ("/") is used. When `show_header` is true, a header line
 * containing the target path followed by ':' is written before the listing. If the directory
 * cannot be listed, an error message is written to `io`.
 *
 * @param path Path of the directory to list, or NULL to list "/".
 * @param io   Shell I/O interface used for all output.
 * @param show_header If true, print a header line with the target path before the listing.
 */
static void ls_list_path(const char *path, const struct shell_io *io, bool show_header)
{
    const char *target = path ? path : "/";

    if (show_header) {
        shell_io_write_string(io, target);
        shell_io_write_string(io, ":\n");
    }

    struct ls_ctx ctx = { io };
    if (!fs_list(target, ls_emit, &ctx)) {
        ls_print_error(io, target);
    }
}

/**
 * Handle the "ls" shell command and write directory listings to the provided shell I/O.
 *
 * When no path arguments are given, lists the current working directory. If the
 * filesystem is not ready, writes "ls: filesystem not available" to the IO.
 * For each provided path, resolves the path before listing; if resolution
 * fails, writes a "cannot access" error for that path and continues. When more
 * than one path is listed, prints a header for each listing and separates
 * consecutive listings with a blank line.
 *
 * @param argc Number of arguments in argv (command name plus any paths).
 * @param argv Argument vector; argv[1]..argv[argc-1] are treated as paths to list.
 * @param io   Shell I/O interface used for all output.
 */
static void ls_handler(int argc, char **argv, const struct shell_io *io)
{
    if (!fs_ready()) {
        shell_io_write_string(io, "ls: filesystem not available\n");
        return;
    }

    if (argc < 2) {
        ls_list_path(shell_get_cwd(), io, false);
        return;
    }

    bool show_header = argc > 2;
    for (int i = 1; i < argc; ++i) {
        char resolved[SHELL_PATH_MAX];
        if (!shell_resolve_path(argv[i], resolved, sizeof(resolved))) {
            ls_print_error(io, argv[i]);
            continue;
        }

        ls_list_path(resolved, io, show_header);
        if (show_header && i + 1 < argc) {
            shell_io_write_string(io, "\n");
        }
    }
}

const struct shell_command shell_command_ls = {
    .name = "ls",
    .help = "List directory contents",
    .handler = ls_handler,
};
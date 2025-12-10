#include <lux/fs.h>
#include <lux/printf.h>
#include <lux/shell.h>

struct ls_ctx {
    const struct shell_io *io;
};

static void ls_emit(const struct fs_dirent *entry, void *user_data)
{
    struct ls_ctx *ctx = (struct ls_ctx *)user_data;
    if (!ctx || !ctx->io) {
        return;
    }

    char line[96];
    snprintf(line, sizeof(line), "%c %8lu %s\n",
             entry->is_dir ? 'd' : '-',
             (unsigned long)entry->size,
             entry->name);
    shell_io_write_string(ctx->io, line);
}

static void ls_print_error(const struct shell_io *io, const char *path)
{
    shell_io_write_string(io, "ls: cannot access ");
    shell_io_write_string(io, path ? path : "/");
    shell_io_write_string(io, "\n");
}

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

static void ls_handler(int argc, char **argv, const struct shell_io *io)
{
    if (!fs_ready()) {
        shell_io_write_string(io, "ls: filesystem not available\n");
        return;
    }

    if (argc < 2) {
        ls_list_path("/", io, false);
        return;
    }

    bool show_header = argc > 2;
    for (int i = 1; i < argc; ++i) {
        ls_list_path(argv[i], io, show_header);
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
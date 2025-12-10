#include <lux/fs.h>
#include <lux/shell.h>

#define CAT_BUFFER_SIZE 512u

static void cat_print_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: cat <path> [path...]\n");
}

static void cat_print_error(const struct shell_io *io, const char *path, const char *reason)
{
    shell_io_write_string(io, "cat: ");
    shell_io_write_string(io, path);
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

static bool cat_stream_file(const char *path, const struct shell_io *io)
{
    struct fs_stat stats;
    if (!fs_stat_path(path, &stats)) {
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
        if (!fs_read(path, offset, buffer, chunk, &bytes_read)) {
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
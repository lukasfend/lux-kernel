#include <lux/fs.h>
#include <lux/shell.h>

static void touch_usage(const struct shell_io *io)
{
    shell_io_write_string(io, "Usage: touch <path> [path...]\n");
    shell_io_write_string(io, "Pipe data into touch to overwrite a single file.\n");
}

static void touch_print_error(const struct shell_io *io, const char *path, const char *reason)
{
    shell_io_write_string(io, "touch: ");
    shell_io_write_string(io, path);
    shell_io_write_string(io, ": ");
    shell_io_write_string(io, reason);
    shell_io_write_string(io, "\n");
}

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
        if (!fs_touch(path)) {
            touch_print_error(io, path, "cannot create file");
            continue;
        }

        if (pipe_len) {
            if (!fs_write(path, 0, pipe_data, pipe_len, true)) {
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

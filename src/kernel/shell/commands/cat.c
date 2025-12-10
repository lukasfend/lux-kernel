#include <lux/shell.h>

static void cat_handler(int argc, char **argv, const struct shell_io *io)
{
    if (argc < 2) {
        if (io && io->input && io->input_len) {
            shell_io_write(io, io->input, io->input_len);
            return;
        }
        shell_io_write_string(io, "Usage: cat <path>\n");
        return;
    }

    shell_io_write_string(io, "Filesystem support is not available yet.\n");
    shell_io_write_string(io, "Stub 'cat' command cannot read ");
    shell_io_write_string(io, argv[1]);
    shell_io_write_string(io, " until a block device driver is added.\n");
}

const struct shell_command shell_command_cat = {
    .name = "cat",
    .help = "Display file contents (stub)",
    .handler = cat_handler,
};

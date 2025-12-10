#include <lux/shell.h>

/**
 * Handle the shell "cat" command (stub implementation).
 *
 * If no path argument is provided, echoes available input from `io` back to the output;
 * otherwise prints a usage message. If a path argument is provided, writes messages
 * indicating filesystem support is unavailable and that the specified path cannot be read.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector; `argv[1]` is treated as the path when present.
 * @param io   Shell I/O context; may be NULL. If `io` is non-NULL and `io->input`/`io->input_len`
 *             are present, their contents are echoed when no path is supplied.
 */
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
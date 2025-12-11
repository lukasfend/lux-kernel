#include <lux/fs.h>
#include <lux/shell.h>

static void cd_handler(int argc, char **argv, const struct shell_io *io)
{
    const char *target = (argc >= 2) ? argv[1] : "/home";

    if (!fs_ready()) {
        shell_io_write_string(io, "cd: filesystem not available\n");
        return;
    }

    if (!shell_set_cwd(target)) {
        shell_io_write_string(io, "cd: no such directory: ");
        shell_io_write_string(io, target);
        shell_io_write_string(io, "\n");
    }
}

const struct shell_command shell_command_cd = {
    .name = "cd",
    .help = "Change the current directory",
    .handler = cd_handler,
};

#include <lux/fs.h>
#include <lux/shell.h>

/**
 * Handle the "cd" shell command and change the current working directory.
 *
 * Uses argv[1] as the target directory if provided; otherwise defaults to "/home".
 * If the filesystem is not available, writes "cd: filesystem not available" to the provided IO and returns.
 * If changing to the target directory fails, writes "cd: no such directory: <target>" to the provided IO.
 *
 * @param argc Number of command arguments.
 * @param argv Command arguments array; argv[1] is used as the target directory when present.
 * @param io   IO interface used for writing output and error messages.
 */
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
#include <lux/shell.h>
#include <lux/tty.h>

static void cat_handler(int argc, char **argv)
{
    if (argc < 2) {
        tty_write_string("Usage: cat <path>\n");
        return;
    }

    tty_write_string("Filesystem support is not available yet.\n");
    tty_write_string("Stub 'cat' command cannot read ");
    tty_write_string(argv[1]);
    tty_write_string(" until a block device driver is added.\n");
}

const struct shell_command shell_command_cat = {
    .name = "cat",
    .help = "Display file contents (stub)",
    .handler = cat_handler,
};

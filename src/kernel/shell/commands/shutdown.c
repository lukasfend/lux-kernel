#include <lux/io.h>
#include <lux/shell.h>
#include <lux/tty.h>

static void shutdown_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tty_write_string("Powering off...\n");

    // Attempt to power off via common QEMU ACPI ports.
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);

    for (;;) {
        __asm__ volatile("hlt");
    }
}

const struct shell_command shell_command_shutdown = {
    .name = "shutdown",
    .help = "Power off the machine",
    .handler = shutdown_handler,
};

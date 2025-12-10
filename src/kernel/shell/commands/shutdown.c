#include <lux/io.h>
#include <lux/shell.h>
#include <lux/tty.h>

/**
 * Shut down the system by attempting ACPI power-off and stopping the CPU.
 *
 * Writes "Powering off...\n" to the TTY, issues ACPI power-off values (0x2000)
 * to I/O ports 0x604 and 0xB004, then halts the CPU in an infinite loop.
 */
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
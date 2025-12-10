#include <lux/io.h>
#include <lux/shell.h>
#include <lux/time.h> 


/**
 * Shut down the system by attempting ACPI power-off and stopping the CPU.
 *
 * Writes "Powering off...\n" to the TTY, issues ACPI power-off values (0x2000)
 * to I/O ports 0x604 and 0xB004, then halts the CPU in an infinite loop.
 */
/**
 * Shut down the system by attempting ACPI power-off and halting the CPU.
 *
 * Writes "Powering off...\n" to the provided shell I/O, waits briefly, issues
 * ACPI power-off commands to common virtualization ports (0x604 and 0xB004),
 * then enters an infinite halt loop and does not return.
 *
 * @param io Shell I/O interface used to print the shutdown message.
 */
static void shutdown_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    shell_io_write_string(io, "Powering off...\n");
    sleep_ms(1000);

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
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
 * Waits up to the specified number of milliseconds, aborting early if a shell interrupt is detected.
 *
 * @param milliseconds Number of milliseconds to wait.
 * @returns `true` if the full delay completed, `false` if interrupted by a shell interrupt.
 */
static bool wait_for_shutdown_delay(uint32_t milliseconds)
{
    for (uint32_t elapsed = 0; elapsed < milliseconds; ++elapsed) {
        if (shell_command_should_stop()) {
            return false;
        }
        sleep_ms(1);
    }

    return true;
}

/**
 * Handle the "shutdown" shell command and initiate the system power-off sequence.
 *
 * Writes a shutdown message to the provided shell IO, waits up to 1000 milliseconds
 * for any interrupt (returning early if interrupted), attempts an ACPI power-off
 * via common QEMU ports, and then halts the CPU indefinitely.
 *
 * @param argc Unused argument count (explicitly ignored).
 * @param argv Unused argument vector (explicitly ignored).
 * @param io   Shell IO used to write the shutdown message.
 */
static void shutdown_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    shell_io_write_string(io, "Powering off...\n");

    if (!wait_for_shutdown_delay(1000u)) {
        return;
    }

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
/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Kernel entry point that initializes the TTY and launches the shell.
 */
#include <stdbool.h>

#include <lux/ata.h>
#include <lux/interrupt.h>
#include <lux/fs.h>
#include <lux/memory.h>
#include <lux/shell.h>
#include <lux/tty.h>

/**
 * Display the kernel banner on the primary TTY.
 *
 * Writes the fixed banner string "lux-kernel by Lukas Fend (c) 2025\n" to the configured terminal output.
 */
static void banner(void)
{
    tty_write_string("lux-kernel by Lukas Fend (c) 2025\n");
}

/**
 * Initialize core subsystems, start the interactive shell, and enter an idle halt loop.
 *
 * Performs early kernel setup (heap and TTY), displays the kernel banner, launches the shell,
 * and if the shell ever returns, halts the CPU in an infinite idle loop.
 */
void kernel(void)
{
    heap_init();
    tty_init(0x1F);
    interrupt_dispatcher_init();

    if (!ata_pio_init()) {
        tty_write_string("[disk] ATA PIO init failed; filesystem disabled.\n");
    } else if (!fs_mount()) {
        tty_write_string("[disk] Filesystem mount failed; continuing without storage.\n");
    } else {
        tty_write_string("[disk] Filesystem mounted successfully.\n");
    }

    banner();
    shell_run();

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}
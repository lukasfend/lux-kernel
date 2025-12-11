/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Kernel entry point that initializes the TTY and launches the shell.
 */
#include <stdbool.h>

#include <lux/ata.h>
#include <lux/idt.h>
#include <lux/interrupt.h>
#include <lux/fs.h>
#include <lux/memory.h>
#include <lux/process.h>
#include <lux/shell.h>
#include <lux/timer.h>
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
 * Initialize core kernel subsystems, start the interactive shell, and halt the CPU if the shell exits.
 *
 * Performs early kernel setup (heap allocator, TTY, and interrupt dispatcher), attempts disk and
 * filesystem initialization (may continue without storage if those steps fail), displays the kernel
 * banner, and launches the shell. If the shell ever returns, the function enters an infinite halted loop.
 */
void kernel(void)
{
    heap_init();
    tty_init(0x1F);
    interrupt_dispatcher_init();
    process_manager_init();
    
    /* Initialize the IDT and remap the PIC for interrupt-driven input */
    idt_init();
    interrupt_enable();
    pit_init();

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
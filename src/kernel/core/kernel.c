/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Kernel entry point that initializes the TTY and launches the shell.
 */
#include <lux/memory.h>
#include <lux/shell.h>
#include <lux/tty.h>

static void banner(void)
{
    tty_write_string("lux-kernel\n");
    tty_write_string("Runtime primitives online.\n");
}

void kernel(void)
{
    heap_init();
    tty_init(0x1F);
    banner();
    shell_run();

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

#include <lux/shell.h>
#include <lux/tty.h>

static void banner(void)
{
    tty_write_string("lux-kernel\n");
    tty_write_string("Runtime primitives online.\n");
}

void kmain(void)
{
    tty_init(0x1F);
    banner();
    shell_run();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

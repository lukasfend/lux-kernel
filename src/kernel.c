#include <stdint.h>
#include <tty.h>

static void banner(void)
{
    tty_write_string("lux-kernel\n");
    tty_write_string("Runtime primitives online.\n\n");
}

void kmain(void)
{
    tty_init(0x1F);
    banner();
    tty_write_string("You can now experiment with freestanding C code.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

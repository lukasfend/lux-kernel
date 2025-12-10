/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Linker script describing the freestanding kernel layout.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    . = 0x0010000;

    .text : ALIGN(16)
    {
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss*)
    }
}
/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: IDT initialization and interrupt handler support for x86.
 */
#include <lux/idt.h>
#include <lux/keyboard.h>
#include <lux/io.h>

/**
 * Read the keyboard scancode from I/O port 0x60 and forward it to the keyboard driver for processing in interrupt context.
 *
 * Invoked by the IRQ1 assembly handler.
 */
void keyboard_irq_handler_c(void)
{
    /* Read the scancode from the keyboard data port */
    uint8_t scancode = inb(0x60);
    
    /* 
     * Process the scancode using the existing keyboard driver functions.
     * This allows the keyboard driver to maintain its current architecture
     * while switching from polling to interrupt-driven.
     */
    char out_char;
    (void)keyboard_process_scancode_irq(scancode, &out_char);
}
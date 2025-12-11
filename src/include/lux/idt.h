/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: IDT (Interrupt Descriptor Table) setup and PIC remap for x86 protected mode.
 */
#pragma once

#include <stdint.h>

/**
 * Initialize the x86 IDT (Interrupt Descriptor Table) and remap the 8259 PIC.
 *
 * Sets up the IDT with:
 * - Exceptions (0x00-0x1F) with default handlers that halt the CPU
 * - IRQ handlers (0x20-0x2F) that acknowledge and forward to keyboard driver
 * 
 * Remaps the PIC so:
 * - Master PIC interrupts appear as vectors 0x20-0x27
 * - Slave PIC interrupts appear as vectors 0x28-0x2F
 * 
 * After calling this function, the CPU can safely handle hardware interrupts.
 */
void idt_init(void);

/**
 * Enable CPU interrupts (sti instruction).
 */
void interrupt_enable(void);

/**
 * Disable CPU interrupts (cli instruction).
 */
void interrupt_disable(void);

/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: PIT timer and timer interrupt handling for preemptive scheduling.
 */
#include <lux/timer.h>
#include <lux/io.h>
#include <lux/process.h>

/* PIT I/O ports */
#define PIT_CHANNEL_0    0x40
#define PIT_COMMAND      0x43

/* PIT command byte format */
#define PIT_CMD_CHANNEL_0       0x00
#define PIT_CMD_ACCESS_LOHI     0x30
#define PIT_CMD_MODE_RATEGEN    0x04
#define PIT_CMD_BINARY          0x00

/* Tick tracking */
static uint32_t timer_ticks = 0;
static uint32_t timer_ticks_last_process_update = 0;

/**
 * Timer interrupt handler for IRQ0.
 * 
 * Called by the interrupt dispatcher when timer fires.
 * Updates tick count and triggers process scheduling.
 */
static void timer_irq_handler(void)
{
    timer_ticks++;
    
    /* Update process sleep times and schedule preemptively */
    uint32_t elapsed = timer_ticks - timer_ticks_last_process_update;
    if (elapsed > 0) {
        process_update_sleep_times(elapsed);
        process_schedule();
        timer_ticks_last_process_update = timer_ticks;
    }
}

/**
 * Initialize the PIT to fire at 1000 Hz (1 ms per tick).
 * 
 * The PIT channel 0 is set to generate interrupts at a 1 kHz rate.
 * Divisor = 1193180 / 1000 = 1193 (0x4A9)
 */
void pit_init(void)
{
    /* PIT channel 0, access both bytes, mode 2 (rate generator), binary */
    uint8_t cmd = PIT_CMD_CHANNEL_0 | PIT_CMD_ACCESS_LOHI | PIT_CMD_MODE_RATEGEN | PIT_CMD_BINARY;
    outb(PIT_COMMAND, cmd);
    
    /* Set divisor to 1193 for 1000 Hz */
    uint16_t divisor = 1193;
    outb(PIT_CHANNEL_0, (uint8_t)(divisor & 0xFF));        /* Low byte */
    outb(PIT_CHANNEL_0, (uint8_t)((divisor >> 8) & 0xFF)); /* High byte */
    
    timer_ticks = 0;
    timer_ticks_last_process_update = 0;
}

/**
 * Get the current tick count (milliseconds).
 */
uint32_t pit_get_ticks(void)
{
    return timer_ticks;
}

/**
 * C wrapper called from the IRQ0 interrupt handler in idt.asm.
 */
void timer_irq_handler_c(void)
{
    timer_irq_handler();
}

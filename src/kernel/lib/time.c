#include <lux/time.h>

#include <stdint.h>

#define SLEEP_TICK_ITERATIONS 8000u

/**
 * Busy-wait for one calibrated timing tick.
 *
 * Performs a tight loop of SLEEP_TICK_ITERATIONS iterations, issuing a processor
 * PAUSE hint on each iteration to consume time while reducing CPU contention.
 */
static void busy_wait_tick(void)
{
    for (volatile uint32_t i = 0; i < SLEEP_TICK_ITERATIONS; ++i) {
        __asm__ volatile("pause");
    }
}

/**
 * Block execution for the specified number of milliseconds using a busy-wait loop.
 *
 * This function performs a CPU-intensive spin and does not yield the processor
 * or validate the input; timing is approximate and depends on calibration.
 *
 * @param milliseconds Number of milliseconds to sleep.
 */
void sleep_ms(uint32_t milliseconds)
{
    while (milliseconds--) {
        busy_wait_tick();
    }
}
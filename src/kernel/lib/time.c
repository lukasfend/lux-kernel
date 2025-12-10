#include <lux/time.h>

#include <stdint.h>

#define SLEEP_TICK_ITERATIONS 8000u

static void busy_wait_tick(void)
{
    for (volatile uint32_t i = 0; i < SLEEP_TICK_ITERATIONS; ++i) {
        __asm__ volatile("pause");
    }
}

void sleep_ms(uint32_t milliseconds)
{
    while (milliseconds--) {
        busy_wait_tick();
    }
}

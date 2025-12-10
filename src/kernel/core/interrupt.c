/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Simple interrupt dispatcher delivering software signals to subscribers.
 */
#include <lux/interrupt.h>

#include <stdbool.h>
#include <stddef.h>

#define INTERRUPT_MAX_HANDLERS 16

struct interrupt_subscription {
    enum interrupt_signal signal;
    interrupt_handler_t handler;
    void *context;
    bool active;
};

static struct interrupt_subscription subscriptions[INTERRUPT_MAX_HANDLERS];
static bool dispatcher_ready;

void interrupt_dispatcher_init(void)
{
    if (dispatcher_ready) {
        return;
    }

    for (size_t i = 0; i < INTERRUPT_MAX_HANDLERS; ++i) {
        subscriptions[i].handler = 0;
        subscriptions[i].context = 0;
        subscriptions[i].active = false;
        subscriptions[i].signal = INTERRUPT_SIGNAL_CTRL_C;
    }

    dispatcher_ready = true;
}

int interrupt_subscribe(enum interrupt_signal signal, interrupt_handler_t handler, void *context)
{
    if (!handler || signal >= INTERRUPT_SIGNAL_MAX) {
        return -1;
    }

    for (size_t i = 0; i < INTERRUPT_MAX_HANDLERS; ++i) {
        if (!subscriptions[i].active) {
            subscriptions[i].signal = signal;
            subscriptions[i].handler = handler;
            subscriptions[i].context = context;
            subscriptions[i].active = true;
            return (int)i;
        }
    }

    return -1;
}

bool interrupt_unsubscribe(int id)
{
    if (id < 0 || id >= (int)INTERRUPT_MAX_HANDLERS) {
        return false;
    }

    subscriptions[id].active = false;
    subscriptions[id].handler = 0;
    subscriptions[id].context = 0;
    return true;
}

void interrupt_raise(enum interrupt_signal signal)
{
    if (signal >= INTERRUPT_SIGNAL_MAX) {
        return;
    }

    if (!dispatcher_ready) {
        interrupt_dispatcher_init();
    }

    for (size_t i = 0; i < INTERRUPT_MAX_HANDLERS; ++i) {
        if (!subscriptions[i].active) {
            continue;
        }
        if (subscriptions[i].signal != signal) {
            continue;
        }
        subscriptions[i].handler(signal, subscriptions[i].context);
    }
}

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

/**
 * Initialize the interrupt dispatcher, clearing all subscription slots and marking
 * the dispatcher as ready for use.
 *
 * This function is safe to call multiple times; subsequent calls have no effect
 * after the dispatcher is initialized.
 */
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

/**
 * Register a handler to be invoked when the specified interrupt signal is raised.
 *
 * @param signal Interrupt signal to subscribe to.
 * @param handler Function to call when the signal is raised; must be non-NULL.
 * @param context Opaque pointer passed to the handler when invoked.
 * @returns Subscription id in the range `0`..`INTERRUPT_MAX_HANDLERS - 1` on success, `-1` on failure (invalid `signal` or `handler`, or no available subscription slot).
 */
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

/**
 * Unsubscribe a previously registered interrupt subscription by slot id.
 *
 * Clears the subscription at the given slot (marks it inactive and removes its
 * handler and context).
 *
 * @param id Index of the subscription slot to remove (0 .. INTERRUPT_MAX_HANDLERS - 1).
 * @returns `true` if the slot was valid and cleared, `false` if `id` is out of range.
 */
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

/**
 * Deliver the given interrupt signal to all active subscribers that registered for it.
 *
 * If the signal value is outside the valid range, the call is ignored. The dispatcher
 * is initialized if necessary before any handlers are invoked. For each active subscription
 * whose registered signal matches the provided `signal`, the subscription's handler is
 * called with the same `signal` and the subscription's context pointer.
 *
 * @param signal The interrupt signal to raise and deliver to matching subscribers.
 */
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
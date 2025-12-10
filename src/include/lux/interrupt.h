/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Lightweight interrupt dispatcher for asynchronous kernel notifications.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum interrupt_signal {
    INTERRUPT_SIGNAL_CTRL_C = 0,
    INTERRUPT_SIGNAL_MAX
};

typedef void (*interrupt_handler_t)(enum interrupt_signal signal, void *context);

void interrupt_dispatcher_init(void);
int interrupt_subscribe(enum interrupt_signal signal, interrupt_handler_t handler, void *context);
bool interrupt_unsubscribe(int id);
void interrupt_raise(enum interrupt_signal signal);

#ifdef __cplusplus
}
#endif

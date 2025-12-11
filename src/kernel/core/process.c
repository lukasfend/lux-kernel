/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Process and task management implementation.
 */
#include <lux/process.h>
#include <lux/memory.h>
#include <string.h>

/* Assembly functions for context switching */
extern void process_context_switch(struct process *from, struct process *to);

#define MAX_PROCESSES 16
#define STACK_SIZE_DEFAULT 4096

static struct process process_table[MAX_PROCESSES];
static size_t process_count_active = 0;
static uint32_t next_pid = 1;
static struct process *current_process = NULL;
static size_t current_process_index = 0;

/**
 * Initialize the process management system.
 */
void process_manager_init(void)
{
    memset(process_table, 0, sizeof(process_table));
    process_count_active = 0;
    next_pid = 1;
    current_process = NULL;
    current_process_index = 0;
}

/**
 * Get the currently running process.
 */
struct process *process_current(void)
{
    return current_process;
}

/**
 * Allocate and initialize a new process structure.
 */
static struct process *process_alloc(void (*entry_point)(void), size_t stack_size)
{
    if (process_count_active >= MAX_PROCESSES) {
        return NULL;
    }

    /* Find an empty slot */
    struct process *proc = NULL;
    size_t index = 0;
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].state == PROCESS_STATE_STOPPED && process_table[i].pid == 0) {
            proc = &process_table[i];
            index = i;
            break;
        }
    }

    if (!proc) {
        return NULL;
    }

    /* Allocate stack */
    uint32_t *stack = (uint32_t *)malloc(stack_size);
    if (!stack) {
        return NULL;
    }

    /* Initialize process structure */
    memset(proc, 0, sizeof(*proc));
    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->stack = stack;
    proc->stack_size = stack_size;
    proc->priority = 128; /* Medium priority */
    
    /* Initialize CPU context with entry point */
    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.esp = (uint32_t)(stack + stack_size / sizeof(uint32_t) - 1);
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = 0x200; /* IF flag set, interrupts enabled */
    proc->context.ebp = proc->context.esp;

    process_count_active++;
    return proc;
}

/**
 * Create a new process.
 */
int process_create(void (*entry_point)(void), size_t stack_size)
{
    if (!entry_point) {
        return -1;
    }

    if (stack_size == 0) {
        stack_size = STACK_SIZE_DEFAULT;
    }

    struct process *proc = process_alloc(entry_point, stack_size);
    if (!proc) {
        return -1;
    }

    return (int)proc->pid;
}

/**
 * Free a process and its resources.
 */
static void process_free(struct process *proc)
{
    if (!proc || proc->pid == 0) {
        return;
    }

    if (proc->stack) {
        free(proc->stack);
        proc->stack = NULL;
    }

    proc->pid = 0;
    proc->state = PROCESS_STATE_STOPPED;
    if (process_count_active > 0) {
        process_count_active--;
    }
}

/**
 * Terminate the current process.
 */
void process_exit(void)
{
    if (!current_process) {
        return;
    }

    process_free(current_process);
    current_process = NULL;
    
    /* Schedule next process */
    process_schedule();
}

/**
 * Put the current process to sleep.
 */
void process_sleep(uint32_t ticks)
{
    if (!current_process) {
        return;
    }

    current_process->state = PROCESS_STATE_SLEEPING;
    current_process->wake_time_ticks = ticks;
    
    /* Schedule next process */
    process_schedule();
}

/**
 * Yield the current process.
 */
void process_yield(void)
{
    if (current_process && current_process->state == PROCESS_STATE_RUNNING) {
        current_process->state = PROCESS_STATE_READY;
    }

    process_schedule();
}

/**
 * Get the count of active processes.
 */
size_t process_count(void)
{
    return process_count_active;
}

/**
 * Get process by index.
 */
struct process *process_get_by_index(size_t index)
{
    size_t count = 0;
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].pid != 0) {
            if (count == index) {
                return &process_table[i];
            }
            count++;
        }
    }
    return NULL;
}

/**
 * Get process by PID.
 */
struct process *process_get_by_pid(uint32_t pid)
{
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

/**
 * Kill a process by PID.
 */
bool process_kill(uint32_t pid)
{
    struct process *proc = process_get_by_pid(pid);
    if (!proc) {
        return false;
    }

    if (proc == current_process) {
        process_exit();
    } else {
        process_free(proc);
    }

    return true;
}

/**
 * Simple round-robin scheduler: find next ready process.
 */
void process_schedule(void)
{
    struct process *next_process = NULL;
    
    /* If no processes, just return */
    if (process_count_active == 0) {
        current_process = NULL;
        return;
    }

    /* Try to find the next ready process starting from current_process_index */
    size_t start_index = (current_process_index + 1) % MAX_PROCESSES;
    size_t index = start_index;

    do {
        if (process_table[index].pid != 0 && process_table[index].state == PROCESS_STATE_READY) {
            next_process = &process_table[index];
            current_process_index = index;
            next_process->state = PROCESS_STATE_RUNNING;
            break;
        }

        index = (index + 1) % MAX_PROCESSES;
    } while (index != start_index);

    /* No ready process found, keep current running if it exists */
    if (!next_process) {
        if (current_process && current_process->state == PROCESS_STATE_RUNNING) {
            return;
        }

        /* Fallback: find any non-stopped process */
        for (size_t i = 0; i < MAX_PROCESSES; ++i) {
            if (process_table[i].pid != 0 && process_table[i].state != PROCESS_STATE_STOPPED) {
                next_process = &process_table[i];
                current_process_index = i;
                next_process->state = PROCESS_STATE_RUNNING;
                break;
            }
        }
    }

    /* Perform the actual context switch if we have a new process */
    if (next_process && next_process != current_process) {
        struct process *old_process = current_process;
        current_process = next_process;
        process_context_switch(old_process, next_process);
    }
}

/**
 * Update sleeping processes.
 */
void process_update_sleep_times(uint32_t ticks_elapsed)
{
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].pid != 0 && process_table[i].state == PROCESS_STATE_SLEEPING) {
            if (process_table[i].wake_time_ticks <= ticks_elapsed) {
                process_table[i].wake_time_ticks = 0;
                process_table[i].state = PROCESS_STATE_READY;
            } else {
                process_table[i].wake_time_ticks -= ticks_elapsed;
            }
        }
    }
}

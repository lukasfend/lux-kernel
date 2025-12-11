/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Process and task management for multitasking kernel.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Process/task states.
 */
enum process_state {
    PROCESS_STATE_READY,       /* Ready to run, waiting for CPU time */
    PROCESS_STATE_RUNNING,     /* Currently executing */
    PROCESS_STATE_SLEEPING,    /* Sleeping until a wake time */
    PROCESS_STATE_STOPPED,     /* Terminated or stopped */
};

/**
 * CPU context saved/restored during context switches.
 * Matches the x86 register layout we'll save in assembly.
 */
struct cpu_context {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
};

/**
 * Process control block (PCB) representing a single process/task.
 */
struct process {
    uint32_t pid;                    /* Process ID */
    enum process_state state;        /* Current process state */
    struct cpu_context context;      /* Saved CPU context for switching */
    uint32_t *stack;                 /* Allocated stack for this process */
    size_t stack_size;               /* Size of allocated stack */
    uint32_t wake_time_ticks;        /* Tick count when sleeping process should wake */
    uint8_t priority;                /* Priority (0=lowest, 255=highest) */
};

/**
 * Initialize the process/task management system.
 * Must be called once during kernel initialization before any processes are created.
 */
void process_manager_init(void);

/**
 * Get the currently running process.
 * 
 * @returns Pointer to the current process structure, or NULL if no process is running.
 */
struct process *process_current(void);

/**
 * Create a new process with the given entry point and stack size.
 * 
 * @param entry_point Function pointer to process entry (will be called as void entry_point(void))
 * @param stack_size Size of stack to allocate for the process
 * @returns PID of the new process, or -1 on failure
 */
int process_create(void (*entry_point)(void), size_t stack_size);

/**
 * Terminate the current process and switch to the next ready process.
 * 
 * The current process will be marked as stopped and freed.
 */
void process_exit(void);

/**
 * Put the current process to sleep for the specified number of ticks (milliseconds).
 * 
 * The scheduler will wake the process when the specified time has elapsed.
 * This yields the CPU to another ready process.
 * 
 * @param ticks Number of ticks (approximately milliseconds) to sleep
 */
void process_sleep(uint32_t ticks);

/**
 * Yield the current process to allow other processes to run.
 * 
 * The current process goes to the back of the ready queue.
 */
void process_yield(void);

/**
 * Get the number of active processes (not stopped).
 * 
 * @returns Count of processes in ready, running, or sleeping states
 */
size_t process_count(void);

/**
 * Get process information by index for iteration/listing.
 * 
 * @param index Index in the process list (0 to process_count()-1)
 * @returns Pointer to process structure, or NULL if index out of range
 */
struct process *process_get_by_index(size_t index);

/**
 * Get process information by PID.
 * 
 * @param pid Process ID to search for
 * @returns Pointer to process structure, or NULL if not found
 */
struct process *process_get_by_pid(uint32_t pid);

/**
 * Terminate a process by PID.
 * 
 * @param pid Process ID to terminate
 * @returns true if process was found and terminated, false otherwise
 */
bool process_kill(uint32_t pid);

/**
 * Perform a context switch to the next ready process.
 * 
 * Called by the timer interrupt handler to switch processes periodically.
 * The current process's context is saved and the next ready process is switched to.
 */
void process_schedule(void);

/**
 * Update sleeping processes and wake those whose time has elapsed.
 * 
 * Called periodically (e.g., from timer interrupt) to advance the scheduler.
 * 
 * @param ticks_elapsed Number of ticks that have elapsed since last call
 */
void process_update_sleep_times(uint32_t ticks_elapsed);

/**
 * Perform an actual context switch between processes.
 * 
 * This is an assembly function that saves the current process's CPU context
 * and restores the target process's context, performing the actual task switch.
 * 
 * @param from Current process (may be NULL if no current process)
 * @param to Target process to switch to
 */
void process_context_switch(struct process *from, struct process *to);

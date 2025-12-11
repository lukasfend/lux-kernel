/*
 * Date: 2025-12-11 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: ps command - list running processes.
 */
#include <lux/process.h>
#include <lux/shell.h>
#include <lux/printf.h>

static const char *process_state_name(enum process_state state)
{
    switch (state) {
    case PROCESS_STATE_READY:
        return "READY";
    case PROCESS_STATE_RUNNING:
        return "RUN";
    case PROCESS_STATE_SLEEPING:
        return "SLEEP";
    case PROCESS_STATE_STOPPED:
        return "STOP";
    default:
        return "?";
    }
}

static void ps_handler(int argc, char **argv, const struct shell_io *io)
{
    (void)argc;
    (void)argv;

    shell_io_write_string(io, "PID  STATE   PRIORITY\n");
    shell_io_write_string(io, "---  -----   --------\n");

    size_t count = process_count();
    for (size_t i = 0; i < count; ++i) {
        struct process *proc = process_get_by_index(i);
        if (!proc) {
            continue;
        }

        char buffer[64];
        int len = snprintf(buffer, sizeof(buffer), "%3u  %-6s  %u\n",
                          proc->pid, 
                          process_state_name(proc->state),
                          proc->priority);
        
        if (len > 0 && len < (int)sizeof(buffer)) {
            shell_io_write_string(io, buffer);
        }
    }

    if (count == 0) {
        shell_io_write_string(io, "(no processes)\n");
    }
}

const struct shell_command ps_command = {
    .name = "ps",
    .help = "List running processes",
    .handler = ps_handler
};

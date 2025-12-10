#include <lux/keyboard.h>
#include <lux/shell.h>
#include <string.h>
#include <lux/tty.h>

#define INPUT_BUFFER_SIZE 128
#define MAX_ARGS 8

struct command {
    const char *name;
    const char *help;
    void (*handler)(int argc, char **argv);
};

static void cmd_help(int argc, char **argv);
static void cmd_echo(int argc, char **argv);

static const struct command commands[] = {
    {"help", "List available commands", cmd_help},
    {"echo", "Echo the provided text", cmd_echo},
};

static void prompt(void)
{
    tty_write_string("lux> ");
}

static size_t read_line(char *buffer, size_t capacity)
{
    size_t len = 0;

    while (len + 1 < capacity) {
        char c = keyboard_read_char();
        if (!c) {
            continue;
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            tty_putc('\n');
            break;
        }

        if (c == '\b') {
            if (len) {
                --len;
                tty_putc('\b');
            }
            continue;
        }

        buffer[len++] = c;
        tty_putc(c);
    }

    buffer[len] = '\0';
    return len;
}

static int tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *cursor = line;

    while (*cursor && argc < max_args) {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (!*cursor) {
            break;
        }

        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor == ' ') {
            *cursor++ = '\0';
        }
    }

    return argc;
}

static const struct command *find_command(const char *name)
{
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return 0;
}

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tty_write_string("Available commands:\n");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        tty_write_string("  ");
        tty_write_string(commands[i].name);
        tty_write_string(" - ");
        tty_write_string(commands[i].help);
        tty_putc('\n');
    }
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        tty_write_string(argv[i]);
        if (i + 1 < argc) {
            tty_putc(' ');
        }
    }
    tty_putc('\n');
}

void shell_run(void)
{
    char buffer[INPUT_BUFFER_SIZE];
    char *argv[MAX_ARGS];

    tty_write_string("Type 'help' for a list of commands.\n");

    for (;;) {
        prompt();
        size_t len = read_line(buffer, sizeof(buffer));
        if (!len) {
            continue;
        }

        int argc = tokenize(buffer, argv, MAX_ARGS);
        if (!argc) {
            continue;
        }

        const struct command *cmd = find_command(argv[0]);
        if (!cmd) {
            tty_write_string("Unknown command: ");
            tty_write_string(argv[0]);
            tty_putc('\n');
            continue;
        }

        cmd->handler(argc, argv);
    }
}

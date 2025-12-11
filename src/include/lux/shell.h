/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Entry point declaration for the interactive shell.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct shell_io {
	const char *input;
	size_t input_len;
	void (*write)(void *context, const char *data, size_t len);
	void *context;
};

#define SHELL_PATH_MAX 256u

void shell_io_write(const struct shell_io *io, const char *data, size_t len);
void shell_io_write_string(const struct shell_io *io, const char *str);
void shell_io_putc(const struct shell_io *io, char c);
bool shell_interrupt_poll(void);

struct shell_command {
	const char *name;
	const char *help;
	void (*handler)(int argc, char **argv, const struct shell_io *io);
};

const struct shell_command *const *shell_builtin_commands(size_t *count);

void shell_run(void);

const char *shell_get_cwd(void);
bool shell_resolve_path(const char *path, char *out, size_t out_len);
bool shell_set_cwd(const char *path);

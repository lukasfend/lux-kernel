/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Entry point declaration for the interactive shell.
 */
#pragma once

#include <stddef.h>

struct shell_command {
	const char *name;
	const char *help;
	void (*handler)(int argc, char **argv);
};

const struct shell_command *const *shell_builtin_commands(size_t *count);

void shell_run(void);

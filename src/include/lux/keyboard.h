/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Public interface for the PS/2 keyboard driver.
 */
#pragma once

enum keyboard_layout {
	KEYBOARD_LAYOUT_EN_US = 0,
	KEYBOARD_LAYOUT_DE_DE,
};

void keyboard_set_layout(enum keyboard_layout layout);
char keyboard_read_char(void);

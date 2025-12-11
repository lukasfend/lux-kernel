/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Public interface for the PS/2 keyboard driver.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum keyboard_layout {
	KEYBOARD_LAYOUT_EN_US = 0,
	KEYBOARD_LAYOUT_DE_DE,
};

#define KEYBOARD_KEY_ARROW_UP    ((char)0x80)
#define KEYBOARD_KEY_ARROW_DOWN  ((char)0x81)
#define KEYBOARD_KEY_ARROW_LEFT  ((char)0x82)
#define KEYBOARD_KEY_ARROW_RIGHT ((char)0x83)
#define KEYBOARD_KEY_DELETE      ((char)0x84)
#define KEYBOARD_KEY_HOME        ((char)0x85)
#define KEYBOARD_KEY_END         ((char)0x86)

#define KEYBOARD_MOD_SHIFT    0x01u
#define KEYBOARD_MOD_CTRL     0x02u
#define KEYBOARD_MOD_ALTGR    0x04u
#define KEYBOARD_MOD_CAPSLOCK 0x08u

struct keyboard_event {
	char symbol;
	uint8_t modifiers;
	bool pressed;
};

void keyboard_set_layout(enum keyboard_layout layout);
char keyboard_read_char(void);
bool keyboard_poll_char(char *out_char);
bool keyboard_poll_event(struct keyboard_event *event);
bool keyboard_read_event(struct keyboard_event *event);
uint8_t keyboard_modifiers(void);

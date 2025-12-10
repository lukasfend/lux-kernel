/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: PS/2 keyboard driver that polls scancodes and returns characters.
 */
#include <lux/keyboard.h>
#include <lux/io.h>
#include <stdbool.h>
#include <stdint.h>

#define KEYBOARD_MAP_SIZE 128U
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_OUT_BUFFER 0x01

struct keyboard_layout_map {
    const char normal[KEYBOARD_MAP_SIZE];
    const char shifted[KEYBOARD_MAP_SIZE];
    const char altgr[KEYBOARD_MAP_SIZE];
};

static const struct keyboard_layout_map layout_en_us = {
    .normal = {
        /* 0x00 - 0x0F */
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
        /* 0x10 - 0x1F */
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
        /* 0x20 - 0x2F */
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\x60', 0, '\\', 'z', 'x', 'c', 'v',
        /* 0x30 - 0x3F */
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        /* 0x50 - 0x5F */
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    .shifted = {
        /* 0x00 - 0x0F */
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
        /* 0x10 - 0x1F */
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
        /* 0x20 - 0x2F */
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        /* 0x30 - 0x3F */
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        /* 0x50 - 0x5F */
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    .altgr = {
        /* 0x00 - 0x0F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x10 - 0x1F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x20 - 0x2F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x30 - 0x3F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x50 - 0x5F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};

static const struct keyboard_layout_map layout_de_de = {
    .normal = {
        /* 0x00 - 0x0F */
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\xDF', '\xB4', '\b', '\t',
        /* 0x10 - 0x1F */
        'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', '\xFC', '+', '\n', 0, 'a', 's',
        /* 0x20 - 0x2F */
        'd', 'f', 'g', 'h', 'j', 'k', 'l', '\xF6', '\xE4', '^', 0, '#', 'y', 'x', 'c', 'v',
        /* 0x30 - 0x3F */
        'b', 'n', 'm', ',', '.', '-', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        /* 0x50 - 0x5F */
        '2', '3', '0', ',', 0, 0, '<', 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    .shifted = {
        /* 0x00 - 0x0F */
        0, 27, '!', '"', '\xA7', '$', '%', '&', '/', '(', ')', '=', '?', '\x60', '\b', '\t',
        /* 0x10 - 0x1F */
        'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', '\xDC', '*', '\n', 0, 'A', 'S',
        /* 0x20 - 0x2F */
        'D', 'F', 'G', 'H', 'J', 'K', 'L', '\xD6', '\xC4', '\xB0', 0, '\'', 'Y', 'X', 'C', 'V',
        /* 0x30 - 0x3F */
        'B', 'N', 'M', ';', ':', '_', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        /* 0x50 - 0x5F */
        '2', '3', '0', ',', 0, 0, '>', 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    .altgr = {
        /* 0x00 - 0x0F */
        0, 0, 0, '\xB2', '\xB3', 0, 0, 0, '{', '[', ']', '}', '\\', 0, 0, 0,
        /* 0x10 - 0x1F */
        '@', 0, '\x80', /* € */ 0, 0, 0, 0, 0, 0, 0, 0, '~', 0, 0, 0, 0,
        /* 0x20 - 0x2F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x30 - 0x3F */
        0, 0, '\xB5', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x50 - 0x5F */
        0, 0, 0, 0, 0, 0, '|', 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x70 - 0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};

static const struct keyboard_layout_map *current_layout = &layout_de_de;

static bool left_shift_pressed;
static bool right_shift_pressed;
static bool caps_lock_active;
static bool extended_scancode_pending;
static bool alt_gr_active;

static bool is_letter_char(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return true;
    }

    switch ((unsigned char)c) {
    case 0xE4: /* ä */
    case 0xC4: /* Ä */
    case 0xF6: /* ö */
    case 0xD6: /* Ö */
    case 0xFC: /* ü */
    case 0xDC: /* Ü */
        return true;
    default:
        return false;
    }
}

static char translate_scancode(uint8_t scancode)
{
    if (scancode >= KEYBOARD_MAP_SIZE) {
        return 0;
    }

    const struct keyboard_layout_map *layout = current_layout;
    char normal = layout->normal[scancode];
    char shifted = layout->shifted[scancode];
    char altgr = layout->altgr[scancode];
    bool is_letter = is_letter_char(normal);
    bool shift_active = left_shift_pressed || right_shift_pressed;
    bool use_shifted = shift_active;

    if (alt_gr_active && altgr) {
        return altgr;
    }

    if (caps_lock_active && is_letter) {
        use_shifted = !use_shifted;
    }

    if (use_shifted && shifted) {
        return shifted;
    }

    return normal;
}

void keyboard_set_layout(enum keyboard_layout layout)
{
    switch (layout) {
    case KEYBOARD_LAYOUT_DE_DE:
        current_layout = &layout_de_de;
        break;
    case KEYBOARD_LAYOUT_EN_US:
    default:
        current_layout = &layout_en_us;
        break;
    }
}

char keyboard_read_char(void)
{
    for (;;) {
        if (!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUT_BUFFER)) {
            continue;
        }

        uint8_t scancode = inb(KEYBOARD_DATA_PORT);

        if (scancode == 0xE0) {
            extended_scancode_pending = true;
            continue;
        }

        bool is_extended = false;
        if (extended_scancode_pending) {
            is_extended = true;
            extended_scancode_pending = false;
        }

        if (scancode & 0x80) {
            uint8_t make_code = scancode & 0x7F;
            if (make_code == 0x2A) {
                left_shift_pressed = false;
            } else if (make_code == 0x36) {
                right_shift_pressed = false;
            } else if (make_code == 0x38 && is_extended) {
                alt_gr_active = false;
            }
            continue;
        }

        if (is_extended) {
            if (scancode == 0x38) {
                alt_gr_active = true;
            }
            continue;
        }

        if (scancode == 0x2A) {
            left_shift_pressed = true;
            continue;
        }

        if (scancode == 0x36) {
            right_shift_pressed = true;
            continue;
        }

        if (scancode == 0x38) {
            continue;
        }

        if (scancode == 0x3A) {
            caps_lock_active = !caps_lock_active;
            continue;
        }

        char translated = translate_scancode(scancode);
        if (translated) {
            return translated;
        }
    }
}

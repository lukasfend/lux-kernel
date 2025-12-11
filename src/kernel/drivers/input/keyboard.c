/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: PS/2 keyboard driver that polls scancodes and returns characters.
 */
#include <lux/interrupt.h>
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
static bool left_ctrl_pressed;
static bool right_ctrl_pressed;
static bool caps_lock_active;
static bool extended_scancode_pending;
static bool alt_gr_active;

#define KEYBOARD_EVENT_CAPACITY 64u
static struct keyboard_event event_queue[KEYBOARD_EVENT_CAPACITY];
static size_t event_head;
static size_t event_tail;
static size_t event_count;

/**
 * Determine whether a character is an ASCII letter or one of the supported German umlaut letters (ä, Ä, ö, Ö, ü, Ü).
 * @param c Character to test.
 * @returns `true` if `c` is in `a`–`z`, `A`–`Z`, or one of the specified umlaut characters; `false` otherwise.
 */
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

/**
 * Translate a PS/2 scancode into the corresponding character for the active layout.
 *
 * Chooses the AltGr mapping when AltGr is active and an AltGr entry exists; otherwise
 * applies Caps Lock and Shift semantics for letters and selects the shifted mapping
 * if shift is effectively active and available, falling back to the normal mapping.
 *
 * @param scancode Scancode value (index into the current layout maps); values >= KEYBOARD_MAP_SIZE produce no mapping.
 * @returns The mapped character for the scancode, or `0` if the scancode is out of range or unmapped.
 */
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

/**
 * Map a 0xE0-prefixed PS/2 scancode to its corresponding special key sentinel.
 *
 * @param scancode Scancode byte that follows the 0xE0 prefix.
 * @returns Special keyboard sentinel (e.g., `KEYBOARD_KEY_ARROW_*`, `KEYBOARD_KEY_DELETE`, `KEYBOARD_KEY_HOME`, `KEYBOARD_KEY_END`) for recognized extended scancodes, `0` if unrecognized.
 */
static char translate_extended_scancode(uint8_t scancode)
{
    switch (scancode) {
    case 0x48:
        return KEYBOARD_KEY_ARROW_UP;
    case 0x50:
        return KEYBOARD_KEY_ARROW_DOWN;
    case 0x4B:
        return KEYBOARD_KEY_ARROW_LEFT;
    case 0x4D:
        return KEYBOARD_KEY_ARROW_RIGHT;
    case 0x53:
        return KEYBOARD_KEY_DELETE;
    case 0x47:
        return KEYBOARD_KEY_HOME;
    case 0x4F:
        return KEYBOARD_KEY_END;
    default:
        return 0;
    }
}

/**
 * Determine whether either Ctrl modifier key is currently pressed.
 *
 * @returns `true` if either the left or right Ctrl key is pressed, `false` otherwise.
 */
static bool control_modifier_active(void)
{
    return left_ctrl_pressed || right_ctrl_pressed;
}

/**
 * Determines whether a character can be mapped by the Control modifier.
 *
 * @param c Character to test; evaluated as an ASCII letter.
 * @returns `true` if `c` is an ASCII letter `a`–`z` or `A`–`Z`, `false` otherwise.
 */
static bool is_control_mappable(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return true;
    }
    return false;
}

/**
 * Map an alphabetic character to its control-code equivalent; leave other characters unchanged.
 *
 * Converts lowercase `a`–`z` and uppercase `A`–`Z` to control codes 1–26 (`'a'` or `'A'` -> 1, `'b'` or `'B'` -> 2, ..., `'z'` or `'Z'` -> 26). Non-alphabetic characters are returned unchanged.
 *
 * @param c Character to map.
 * @returns The control-code (`1`–`26`) for alphabetic input, or the original character for non-alphabetic input.
 */
static char apply_control_mapping(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 1);
    }
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 1);
    }
    return c;
}

/**
 * Compute the current keyboard modifier bitfield from internal modifier states.
 *
 * @returns Bitfield of active modifiers: combination of KEYBOARD_MOD_SHIFT, KEYBOARD_MOD_CTRL,
 *          KEYBOARD_MOD_ALTGR, and KEYBOARD_MOD_CAPSLOCK.
 */
static uint8_t keyboard_current_modifiers(void)
{
    uint8_t modifiers = 0;
    if (left_shift_pressed || right_shift_pressed) {
        modifiers |= KEYBOARD_MOD_SHIFT;
    }
    if (left_ctrl_pressed || right_ctrl_pressed) {
        modifiers |= KEYBOARD_MOD_CTRL;
    }
    if (alt_gr_active) {
        modifiers |= KEYBOARD_MOD_ALTGR;
    }
    if (caps_lock_active) {
        modifiers |= KEYBOARD_MOD_CAPSLOCK;
    }
    return modifiers;
}

/**
 * Enqueue a translated keyboard symbol into the internal event queue.
 *
 * If `symbol` is 0, no event is queued. When the queue is full, the oldest
 * event is dropped to make room for the new one. The queued event captures
 * the current modifier bitfield and marks the key as pressed. If `symbol`
 * is ASCII 0x03 (ETX), the function also raises the CTRL-C interrupt.
 *
 * @param symbol The translated symbol to enqueue (must be non-zero to be queued).
 */
static void keyboard_queue_event(char symbol)
{
    if (!symbol) {
        return;
    }

    struct keyboard_event event = {
        .symbol = symbol,
        .modifiers = keyboard_current_modifiers(),
        .pressed = true
    };

    if (event_count >= KEYBOARD_EVENT_CAPACITY) {
        event_tail = (event_tail + 1u) % KEYBOARD_EVENT_CAPACITY;
        --event_count;
    }

    event_queue[event_head] = event;
    event_head = (event_head + 1u) % KEYBOARD_EVENT_CAPACITY;
    ++event_count;

    if ((unsigned char)symbol == 0x03u) {
        interrupt_raise(INTERRUPT_SIGNAL_CTRL_C);
    }
}

/**
 * Dequeue the next keyboard event into the provided structure.
 *
 * @param event Pointer to a caller-provided structure that will be filled with the next queued keyboard event.
 *               Must not be NULL.
 * @returns `true` if an event was dequeued into `event`, `false` if the queue was empty or `event` is NULL.
 */
static bool keyboard_dequeue_event(struct keyboard_event *event)
{
    if (!event_count || !event) {
        return false;
    }

    *event = event_queue[event_tail];
    event_tail = (event_tail + 1u) % KEYBOARD_EVENT_CAPACITY;
    --event_count;
    return true;
}

/**
 * Set the active keyboard layout used for scancode-to-character translation.
 *
 * Updates the driver's internal layout selection so subsequent key reads use the
 * specified mapping. Unknown or unsupported `layout` values default to the
 * US English layout.
 *
 * @param layout Enum value selecting the keyboard layout to activate.
 */
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

/**
 * Process a PS/2 scancode, update internal modifier state, and emit a translated character when available.
 *
 * Updates internal modifier state (left/right Shift, left/right Ctrl, Caps Lock, AltGr) and enqueues
 * a keyboard event when a character or recognized extended key is produced.
 *
 * @param scancode PS/2 scancode byte; the high bit set indicates a key release (break code).
 * @param is_extended True if this scancode is part of a preceding 0xE0 extended sequence.
 * @param out_char Pointer to a char that receives the translated character when one is produced.
 * @returns `true` if a translated character was produced and written to *out_char, `false` otherwise.
 */
static bool keyboard_process_scancode(uint8_t scancode, bool is_extended, char *out_char)
{
    if (scancode & 0x80) {
        uint8_t make_code = scancode & 0x7F;
        if (make_code == 0x2A) {
            left_shift_pressed = false;
        } else if (make_code == 0x36) {
            right_shift_pressed = false;
        } else if (make_code == 0x38 && is_extended) {
            alt_gr_active = false;
        } else if (make_code == 0x1D) {
            if (is_extended) {
                right_ctrl_pressed = false;
            } else {
                left_ctrl_pressed = false;
            }
        }
        return false;
    }

    if (is_extended) {
        if (scancode == 0x38) {
            alt_gr_active = true;
            return false;
        }

        if (scancode == 0x1D) {
            right_ctrl_pressed = true;
            return false;
        }

        char extended = translate_extended_scancode(scancode);
        if (extended) {
            *out_char = extended;
            keyboard_queue_event(extended);
            return true;
        }
        return false;
    }

    if (scancode == 0x2A) {
        left_shift_pressed = true;
        return false;
    }

    if (scancode == 0x36) {
        right_shift_pressed = true;
        return false;
    }

    if (scancode == 0x1D) {
        left_ctrl_pressed = true;
        return false;
    }

    if (scancode == 0x38) {
        return false;
    }

    if (scancode == 0x3A) {
        caps_lock_active = !caps_lock_active;
        return false;
    }

    char translated = translate_scancode(scancode);
    if (!translated) {
        return false;
    }

    if (control_modifier_active() && is_control_mappable(translated)) {
        translated = apply_control_mapping(translated);
    }

    *out_char = translated;
    keyboard_queue_event(translated);
    return true;
}

/**
 * Polls the PS/2 controller and produces a translated character when available.
 *
 * Reads available scancode bytes (including extended 0xE0 sequences), updates modifier and translation state, and writes a translated character to `out_char` if one is produced; does not block if no data is available.
 *
 * @param out_char Pointer to a char where the translated character will be stored; must not be NULL.
 * @returns `true` if a mapped character was produced and written to `out_char`, `false` otherwise.
 */
static bool keyboard_scan_symbol(char *out_char)
{
    if (!out_char) {
        return false;
    }

    for (;;) {
        if (!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUT_BUFFER)) {
            return false;
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

        if (keyboard_process_scancode(scancode, is_extended, out_char)) {
            return true;
        }
    }
}

/**
 * Attempt to poll the keyboard and produce a translated character without blocking.
 *
 * @param out_char Pointer to a char that will be set to the translated character when available.
 * @return `true` if a character was produced and written to `out_char`, `false` otherwise.
 */
bool keyboard_poll_char(char *out_char)
{
    return keyboard_scan_symbol(out_char);
}

/**
 * Read the next translated character from the keyboard, waiting until one is available.
 *
 * @returns The next translated character produced by the keyboard (respecting current layout and active modifiers).
 */
char keyboard_read_char(void)
{
    char result;
    while (!keyboard_poll_char(&result)) {
        continue;
    }
    return result;
}

/**
 * Polls for the next keyboard event and writes it to the provided output if one is available.
 * @param event Pointer to a caller-provided struct keyboard_event to receive the dequeued event; must not be NULL.
 * @returns `true` if an event was dequeued and written to `event`, `false` otherwise (including when `event` is NULL or the queue is empty).
 */
bool keyboard_poll_event(struct keyboard_event *event)
{
    if (!event) {
        return false;
    }

    char unused;
    (void)keyboard_scan_symbol(&unused);
    return keyboard_dequeue_event(event);
}

/**
 * Block until the next keyboard event is available and store it in `event`.
 *
 * @param event Destination pointer for the dequeued keyboard event; must not be NULL.
 * @returns `true` if an event was read and written to `event`, `false` if `event` is NULL.
 */
bool keyboard_read_event(struct keyboard_event *event)
{
    if (!event) {
        return false;
    }

    while (!keyboard_poll_event(event)) {
        continue;
    }

    return true;
}

/**
 * Retrieve the currently active keyboard modifier flags.
 *
 * @returns Bitfield of active modifiers as a combination of `KEYBOARD_MOD_*` flags
 *          (e.g., SHIFT, CTRL, ALTGR, CAPSLOCK).
 */
uint8_t keyboard_modifiers(void)
{
    return keyboard_current_modifiers();
}

/**
 * Process a single PS/2 scancode received from an interrupt handler.
 *
 * This function is designed to be called from the IRQ1 (keyboard) interrupt handler
 * and processes a single scancode, updating modifier state and enqueueing events
 * without polling the keyboard port.
 *
 * @param scancode The PS/2 scancode to process (high bit set indicates key release).
 * @param out_char Optional pointer to receive the translated character (may be NULL).
 * @returns true if a character or extended key was produced, false otherwise.
 */
bool keyboard_process_scancode_irq(uint8_t scancode, char *out_char)
{
    char dummy;
    if (!out_char) {
        out_char = &dummy;
    }

    if (scancode == 0xE0) {
        extended_scancode_pending = true;
        return false;
    }

    bool is_extended = false;
    if (extended_scancode_pending) {
        is_extended = true;
        extended_scancode_pending = false;
    }

    return keyboard_process_scancode(scancode, is_extended, out_char);
}

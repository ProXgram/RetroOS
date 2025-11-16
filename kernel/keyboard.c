#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "keyboard.h"
#include "kstring.h"
#include "terminal.h"

struct keymap_entry {
    char normal;
    char shifted;
};

static const struct keymap_entry keymap_set1[128] = {
    [0x02] = {'1', '!'},
    [0x03] = {'2', '@'},
    [0x04] = {'3', '#'},
    [0x05] = {'4', '$'},
    [0x06] = {'5', '%'},
    [0x07] = {'6', '^'},
    [0x08] = {'7', '&'},
    [0x09] = {'8', '*'},
    [0x0A] = {'9', '('},
    [0x0B] = {'0', ')'},
    [0x0C] = {'-', '_'},
    [0x0D] = {'=', '+'},
    [0x0F] = {'\t', '\t'},
    [0x10] = {'q', 'Q'},
    [0x11] = {'w', 'W'},
    [0x12] = {'e', 'E'},
    [0x13] = {'r', 'R'},
    [0x14] = {'t', 'T'},
    [0x15] = {'y', 'Y'},
    [0x16] = {'u', 'U'},
    [0x17] = {'i', 'I'},
    [0x18] = {'o', 'O'},
    [0x19] = {'p', 'P'},
    [0x1A] = {'[', '{'},
    [0x1B] = {']', '}'},
    [0x1C] = {'\n', '\n'},
    [0x1E] = {'a', 'A'},
    [0x1F] = {'s', 'S'},
    [0x20] = {'d', 'D'},
    [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'},
    [0x23] = {'h', 'H'},
    [0x24] = {'j', 'J'},
    [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'},
    [0x27] = {';', ':'},
    [0x28] = {'\'', '"'},
    [0x29] = {'`', '~'},
    [0x2B] = {'\\', '|'},
    [0x2C] = {'z', 'Z'},
    [0x2D] = {'x', 'X'},
    [0x2E] = {'c', 'C'},
    [0x2F] = {'v', 'V'},
    [0x30] = {'b', 'B'},
    [0x31] = {'n', 'N'},
    [0x32] = {'m', 'M'},
    [0x33] = {',', '<'},
    [0x34] = {'.', '>'},
    [0x35] = {'/', '?'},
    [0x39] = {' ', ' '},
};

static bool left_shift_active;
static bool right_shift_active;
static bool caps_lock_active;

static char history_storage[KEYBOARD_HISTORY_LIMIT][KEYBOARD_MAX_LINE];
static size_t history_count;
static size_t history_view_index;
static const char history_empty_line[] = "";

static bool history_matches_last(const char* line) {
    if (history_count == 0) {
        return false;
    }

    size_t last_slot = (history_count - 1) % KEYBOARD_HISTORY_LIMIT;
    return kstrcmp(history_storage[last_slot], line) == 0;
}

static uint16_t keyboard_read_scancode(void) {
    uint16_t prefix = 0;
    for (;;) {
        if ((inb(0x64) & 0x01) == 0) {
            continue;
        }

        uint8_t value = inb(0x60);
        if (value == 0xE0) {
            prefix = 0xE000;
            continue;
        }

        uint16_t scancode = prefix | value;
        prefix = 0;
        return scancode;
    }
}

static bool shift_active(void) {
    return left_shift_active || right_shift_active;
}

static void keyboard_update_modifier(uint8_t scancode, bool released, bool extended) {
    if (extended) {
        return;
    }

    switch (scancode) {
        case 0x2A:
            left_shift_active = !released;
            break;
        case 0x36:
            right_shift_active = !released;
            break;
        case 0x3A:
            if (!released) {
                caps_lock_active = !caps_lock_active;
            }
            break;
        default:
            break;
    }
}

static char keyboard_translate(uint8_t scancode) {
    if (scancode >= sizeof(keymap_set1) / sizeof(keymap_set1[0])) {
        return 0;
    }

    struct keymap_entry entry = keymap_set1[scancode];
    if (entry.normal == 0) {
        return 0;
    }

    char normal = entry.normal;
    char shifted = entry.shifted ? entry.shifted : entry.normal;
    bool is_letter = (normal >= 'a' && normal <= 'z');
    bool use_shift = shift_active();

    if (is_letter) {
        bool should_upper = caps_lock_active ^ use_shift;
        return should_upper ? shifted : normal;
    }

    return use_shift ? shifted : normal;
}

static size_t history_visible_start(void) {
    if (history_count > KEYBOARD_HISTORY_LIMIT) {
        return history_count - KEYBOARD_HISTORY_LIMIT;
    }
    return 0;
}

static const char* history_entry_absolute(size_t absolute_index) {
    if (absolute_index >= history_count) {
        return NULL;
    }

    size_t start = history_visible_start();
    if (absolute_index < start) {
        return NULL;
    }

    size_t slot = absolute_index % KEYBOARD_HISTORY_LIMIT;
    return history_storage[slot];
}

void keyboard_history_record(const char* line) {
    size_t length = kstrlen(line);
    if (length == 0) {
        return;
    }

    if (history_matches_last(line)) {
        history_view_index = history_count;
        return;
    }

    size_t slot = history_count % KEYBOARD_HISTORY_LIMIT;
    size_t copy_length = length;
    if (copy_length >= KEYBOARD_MAX_LINE) {
        copy_length = KEYBOARD_MAX_LINE - 1;
    }

    for (size_t i = 0; i < copy_length; i++) {
        history_storage[slot][i] = line[i];
    }
    history_storage[slot][copy_length] = '\0';
    history_count++;
    history_view_index = history_count;
}

size_t keyboard_history_length(void) {
    return (history_count < KEYBOARD_HISTORY_LIMIT) ? history_count : KEYBOARD_HISTORY_LIMIT;
}

const char* keyboard_history_entry(size_t index) {
    size_t available = keyboard_history_length();
    if (index >= available) {
        return NULL;
    }

    size_t start = history_visible_start();
    size_t absolute_index = start + index;
    return history_entry_absolute(absolute_index);
}

void keyboard_history_reset_iteration(void) {
    history_view_index = history_count;
}

const char* keyboard_history_step(int direction) {
    size_t start = history_visible_start();
    size_t end = history_count;

    if (direction < 0) {
        if (end == 0) {
            return NULL;
        }

        if (history_view_index > end) {
            history_view_index = end;
        }

        if (history_view_index > start) {
            history_view_index--;
        }

        if (history_view_index < start) {
            history_view_index = start;
        }

        return history_entry_absolute(history_view_index);
    }

    if (direction > 0) {
        if (history_view_index >= end) {
            history_view_index = end;
            return NULL;
        }

        history_view_index++;
        if (history_view_index >= end) {
            history_view_index = end;
            return history_empty_line;
        }

        return history_entry_absolute(history_view_index);
    }

    return NULL;
}

static void keyboard_clear_line(size_t cursor, size_t length) {
    if (length == 0) {
        return;
    }

    terminal_begin_batch();
    if (cursor < length) {
        terminal_move_cursor_right(length - cursor);
    }

    for (size_t i = 0; i < length; i++) {
        terminal_write_char('\b');
    }
    terminal_end_batch();
}

static void keyboard_replace_line(const char* source, char* buffer, size_t size,
                                  size_t* length, size_t* cursor) {
    terminal_begin_batch();
    keyboard_clear_line(*cursor, *length);

    size_t copy_length = 0;
    while (copy_length + 1 < size && source[copy_length] != '\0') {
        buffer[copy_length] = source[copy_length];
        terminal_write_char(source[copy_length]);
        copy_length++;
    }

    buffer[copy_length] = '\0';
    *length = copy_length;
    *cursor = copy_length;
    terminal_end_batch();
}

static void keyboard_insert_char(char ch, char* buffer, size_t size,
                                 size_t* length, size_t* cursor) {
    if (*length + 1 >= size) {
        return;
    }

    terminal_begin_batch();
    for (size_t i = *length; i > *cursor; i--) {
        buffer[i] = buffer[i - 1];
    }

    buffer[*cursor] = ch;
    (*length)++;
    (*cursor)++;
    buffer[*length] = '\0';

    size_t tail = *length - *cursor;
    size_t redraw_start = *cursor - 1;
    size_t redraw_length = tail + 1;
    terminal_write(&buffer[redraw_start], redraw_length);
    if (tail > 0) {
        terminal_move_cursor_left(tail);
    }
    terminal_end_batch();
}

static void keyboard_handle_backspace(char* buffer, size_t* length, size_t* cursor) {
    if (*cursor == 0 || *length == 0) {
        return;
    }

    terminal_begin_batch();
    (*cursor)--;
    for (size_t i = *cursor; i < *length; i++) {
        buffer[i] = buffer[i + 1];
    }

    (*length)--;
    buffer[*length] = '\0';

    size_t tail = *length - *cursor;
    terminal_move_cursor_left(1);
    if (tail > 0) {
        terminal_write(&buffer[*cursor], tail);
    }
    terminal_write_char(' ');
    terminal_move_cursor_left(tail + 1);
    terminal_end_batch();
}

static void keyboard_handle_delete(char* buffer, size_t* length, size_t* cursor) {
    if (*cursor >= *length) {
        return;
    }

    terminal_begin_batch();
    for (size_t i = *cursor; i < *length; i++) {
        buffer[i] = buffer[i + 1];
    }

    (*length)--;
    buffer[*length] = '\0';

    size_t tail = *length - *cursor;
    if (tail > 0) {
        terminal_write(&buffer[*cursor], tail);
    }
    terminal_write_char(' ');
    terminal_move_cursor_left(tail + 1);
    terminal_end_batch();
}

char keyboard_get_char(void) {
    for (;;) {
        uint16_t scancode_word = keyboard_read_scancode();
        bool extended = (scancode_word & 0xFF00) == 0xE000;
        uint8_t raw = (uint8_t)(scancode_word & 0xFF);
        bool released = (raw & 0x80) != 0;
        uint8_t scancode = raw & 0x7F;

        keyboard_update_modifier(scancode, released, extended);

        if (released || extended) {
            continue;
        }

        char translated = keyboard_translate(scancode);
        if (translated != 0) {
            return translated;
        }
    }
}

void keyboard_read_line(char* buffer, size_t size) {
    if (size == 0) {
        return;
    }

    size_t length = 0;
    size_t cursor = 0;
    buffer[0] = '\0';
    keyboard_history_reset_iteration();

    for (;;) {
        uint16_t scancode_word = keyboard_read_scancode();
        bool extended = (scancode_word & 0xFF00) == 0xE000;
        uint8_t raw = (uint8_t)(scancode_word & 0xFF);
        bool released = (raw & 0x80) != 0;
        uint8_t scancode = raw & 0x7F;

        keyboard_update_modifier(scancode, released, extended);

        if (released) {
            continue;
        }

        if (extended) {
            switch (scancode) {
                case 0x4B: // Left arrow
                    if (cursor > 0) {
                        cursor--;
                        terminal_move_cursor_left(1);
                    }
                    break;
                case 0x4D: // Right arrow
                    if (cursor < length) {
                        terminal_move_cursor_right(1);
                        cursor++;
                    }
                    break;
                case 0x47: // Home
                    if (cursor > 0) {
                        terminal_move_cursor_left(cursor);
                        cursor = 0;
                    }
                    break;
                case 0x4F: // End
                    if (cursor < length) {
                        terminal_move_cursor_right(length - cursor);
                        cursor = length;
                    }
                    break;
                case 0x53: // Delete
                    keyboard_handle_delete(buffer, &length, &cursor);
                    break;
                case 0x48: { // Up arrow
                    const char* entry = keyboard_history_step(-1);
                    if (entry != NULL) {
                        keyboard_replace_line(entry, buffer, size, &length, &cursor);
                    }
                    break;
                }
                case 0x50: { // Down arrow
                    const char* entry = keyboard_history_step(1);
                    if (entry != NULL) {
                        keyboard_replace_line(entry, buffer, size, &length, &cursor);
                    }
                    break;
                }
                default:
                    break;
            }
            continue;
        }

        switch (scancode) {
            case 0x0E: // Backspace
                keyboard_handle_backspace(buffer, &length, &cursor);
                continue;
            case 0x1C: // Enter
                terminal_write_char('\n');
                buffer[length] = '\0';
                return;
            default:
                break;
        }

        char ch = keyboard_translate(scancode);
        if (ch == 0) {
            continue;
        }

        keyboard_insert_char(ch, buffer, size, &length, &cursor);
    }
}

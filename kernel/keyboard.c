#include <stdint.h>

#include "keyboard.h"
#include "terminal.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static const char scancode_set1[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

char keyboard_get_char(void) {
    for (;;) {
        if ((inb(0x64) & 0x01) == 0) {
            continue;
        }

        uint8_t scancode = inb(0x60);
        if (scancode & 0x80) {
            continue;
        }

        if (scancode < sizeof(scancode_set1)) {
            char ch = scancode_set1[scancode];
            if (ch != 0) {
                return ch;
            }
        }
    }
}

void keyboard_read_line(char* buffer, size_t size) {
    if (size == 0) {
        return;
    }

    size_t length = 0;

    while (length + 1 < size) {
        char c = keyboard_get_char();

        if (c == '\b') {
            if (length > 0) {
                length--;
                terminal_write_char('\b');
            }
            continue;
        }

        if (c == '\n') {
            terminal_write_char('\n');
            break;
        }

        buffer[length++] = c;
        terminal_write_char(c);
    }

    buffer[length] = '\0';
}

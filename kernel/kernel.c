#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static void terminal_clear(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(0x0F, 0x00);
    terminal_buffer = VGA_MEMORY;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static void scroll_if_needed(void) {
    if (terminal_row < VGA_HEIGHT) {
        return;
    }

    const size_t row_size = VGA_WIDTH;
    const size_t total_cells = VGA_WIDTH * VGA_HEIGHT;

    for (size_t i = 0; i < (VGA_HEIGHT - 1) * row_size; i++) {
        terminal_buffer[i] = terminal_buffer[i + row_size];
    }

    for (size_t i = (VGA_HEIGHT - 1) * row_size; i < total_cells; i++) {
        terminal_buffer[i] = vga_entry(' ', terminal_color);
    }

    terminal_row = VGA_HEIGHT - 1;
}

static void terminal_setcell(size_t x, size_t y, char c) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, terminal_color);
}

static void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        scroll_if_needed();
        return;
    }

    if (c == '\r') {
        terminal_column = 0;
        return;
    }

    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
        }
        terminal_setcell(terminal_column, terminal_row, ' ');
        return;
    }

    terminal_setcell(terminal_column, terminal_row, c);
    terminal_column++;
    if (terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        terminal_row++;
        scroll_if_needed();
    }
}

static void terminal_write(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
    }
}

static void terminal_writestring(const char* data) {
    terminal_write(data);
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

static char keyboard_get_char(void) {
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

static void read_line(char* buffer, size_t size) {
    if (size == 0) {
        return;
    }

    size_t length = 0;

    while (length + 1 < size) {
        char c = keyboard_get_char();

        if (c == '\b') {
            if (length > 0) {
                length--;
                terminal_putchar('\b');
            }
            continue;
        }

        if (c == '\n') {
            terminal_putchar('\n');
            break;
        }

        buffer[length++] = c;
        terminal_putchar(c);
    }

    buffer[length] = '\0';
}

void kmain(void) {
    terminal_clear();
    terminal_writestring("RetroOS 64-bit demo kernel\n");
    terminal_writestring("Hello, world!\n\n");
    terminal_writestring("Type something and press Enter: ");

    char input[128];
    read_line(input, sizeof(input));

    terminal_writestring("You typed: ");
    terminal_writestring(input);
    terminal_writestring("\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

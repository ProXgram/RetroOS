#include "terminal.h"
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;
static size_t terminal_width = VGA_WIDTH;
static size_t terminal_height = VGA_HEIGHT;

enum terminal_mode {
    TERMINAL_MODE_VGA_TEXT = 0,
    TERMINAL_MODE_FRAMEBUFFER,
};

static enum terminal_mode current_mode = TERMINAL_MODE_VGA_TEXT;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void terminal_update_cursor(void) {
    const uint16_t position = (uint16_t)(terminal_row * VGA_WIDTH + terminal_column);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

static void scroll_if_needed(void) {
    if (terminal_row < terminal_height) {
        return;
    }

    const size_t row_size = VGA_WIDTH;
    const size_t used_rows = terminal_height;
    const size_t total_cells = used_rows * row_size;

    for (size_t i = 0; i < (used_rows - 1) * row_size; i++) {
        terminal_buffer[i] = terminal_buffer[i + row_size];
    }

    for (size_t i = (used_rows - 1) * row_size; i < total_cells; i++) {
        terminal_buffer[i] = vga_entry(' ', terminal_color);
    }

    terminal_row = terminal_height - 1;
}

static enum terminal_mode detect_terminal_mode(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;

    /* Placeholder: once the bootloader exposes framebuffer details we can
     * select TERMINAL_MODE_FRAMEBUFFER here. */
    return TERMINAL_MODE_VGA_TEXT;
}

static size_t clamp_dimension(uint32_t reported, size_t fallback, size_t limit) {
    size_t value = fallback;

    if (reported > 0) {
        value = reported;
    }

    if (value > limit) {
        value = limit;
    }

    return value;
}

static void configure_geometry(uint32_t width, uint32_t height) {
    terminal_width = clamp_dimension(width, VGA_WIDTH, VGA_WIDTH);
    terminal_height = clamp_dimension(height, VGA_HEIGHT, VGA_HEIGHT);

    if (terminal_height == 0) {
        terminal_height = VGA_HEIGHT;
    }
}

void terminal_initialize(uint32_t width, uint32_t height) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(0x0F, 0x00);
    terminal_buffer = VGA_MEMORY;

    current_mode = detect_terminal_mode(width, height);
    configure_geometry(width, height);

    if (current_mode == TERMINAL_MODE_FRAMEBUFFER) {
        /* Placeholder for a future framebuffer-backed terminal path. */
    }

    for (size_t y = 0; y < terminal_height; y++) {
        for (size_t x = 0; x < terminal_width; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }

    terminal_update_cursor();
}

void terminal_clear(void) {
    terminal_row = 0;
    terminal_column = 0;

    for (size_t y = 0; y < terminal_height; y++) {
        for (size_t x = 0; x < terminal_width; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }

    terminal_update_cursor();
}

void terminal_setcolors(uint8_t fg, uint8_t bg) {
    terminal_color = make_color(fg, bg);
}

static void terminal_setcell(size_t x, size_t y, char c) {
    if (x >= terminal_width || y >= terminal_height) {
        return;
    }
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, terminal_color);
}

void terminal_write_char(char c) {
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        scroll_if_needed();
        terminal_update_cursor();
        return;
    }

    if (c == '\r') {
        terminal_column = 0;
        terminal_update_cursor();
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
        terminal_update_cursor();
        return;
    }

    terminal_setcell(terminal_column, terminal_row, c);
    terminal_column++;
    if (terminal_column >= terminal_width) {
        terminal_column = 0;
        terminal_row++;
        scroll_if_needed();
    }

    terminal_update_cursor();
}

void terminal_write(const char* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        terminal_write_char(data[i]);
    }
}

void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_write_char(data[i]);
    }
}

void terminal_write_uint(unsigned int value) {
    char buffer[12];
    size_t index = 0;

    if (value == 0) {
        terminal_write_char('0');
        return;
    }

    while (value > 0 && index < sizeof(buffer)) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        terminal_write_char(buffer[--index]);
    }
}

void terminal_newline(void) {
    terminal_write_char('\n');
}

void terminal_move_cursor_left(size_t count) {
    while (count > 0) {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = terminal_width - 1;
        } else {
            break;
        }
        count--;
    }
    terminal_update_cursor();
}

void terminal_move_cursor_right(size_t count) {
    while (count > 0) {
        if (terminal_column + 1 < terminal_width) {
            terminal_column++;
        } else {
            if (terminal_row + 1 >= terminal_height) {
                terminal_column = terminal_width - 1;
                break;
            }
            terminal_column = 0;
            terminal_row++;
        }
        count--;
    }
    terminal_update_cursor();
}

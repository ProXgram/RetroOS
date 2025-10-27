#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

#define COMMAND_BUFFER_SIZE 128
#define HISTORY_SIZE 16

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static char command_history[HISTORY_SIZE][COMMAND_BUFFER_SIZE];
static size_t history_count;
static size_t history_start;
static unsigned int history_total;

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

static void terminal_initialize(void) {
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

static void terminal_clear(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_buffer = VGA_MEMORY;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

static void terminal_setcolors(uint8_t fg, uint8_t bg) {
    terminal_setcolor(make_color(fg, bg));
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

static int string_compare(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
        i++;
    }

    if (a[i] == '\0' && b[i] == '\0') {
        return 0;
    }
    return (a[i] == '\0') ? -1 : 1;
}

static int string_ncompare(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
        if (a[i] == '\0' || b[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

static void string_copy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    if (max_len == 0) {
        return;
    }

    while (i + 1 < max_len && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }

    dest[i] = '\0';
}

static void terminal_write_uint(unsigned int value) {
    char buffer[12];
    size_t index = 0;

    if (value == 0) {
        terminal_putchar('0');
        return;
    }

    while (value > 0 && index < sizeof(buffer)) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        terminal_putchar(buffer[--index]);
    }
}

static void terminal_newline(void) {
    terminal_putchar('\n');
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

static void print_banner(void) {
    terminal_writestring("RetroOS 64-bit demo kernel\n");
    terminal_writestring("Welcome to the RetroOS console!\n");
    terminal_writestring("Type 'help' to list available commands.\n");
}

static void print_help(void) {
    terminal_writestring("Available commands:\n");
    terminal_writestring("  help   - Show this help message\n");
    terminal_writestring("  about  - Learn more about RetroOS\n");
    terminal_writestring("  clear  - Clear the screen\n");
    terminal_writestring("  history - Show recent commands\n");
    terminal_writestring("  color <fg> <bg> - Update text colors (0-15)\n");
    terminal_writestring("  echo <text> - Display text back to you\n");
    terminal_writestring("  ideas  - See suggestions for future features\n");
}

static void print_about(void) {
    terminal_writestring("RetroOS is a hobby 64-bit operating system kernel.\n");
    terminal_writestring("It focuses on simplicity and a retro-inspired feel.\n");
}

static void print_history(void) {
    if (history_count == 0) {
        terminal_writestring("No commands in history yet.\n");
        return;
    }

    for (size_t i = 0; i < history_count; i++) {
        size_t index = (history_start + i) % HISTORY_SIZE;
        unsigned int number = history_total - history_count + (unsigned int)i + 1;
        terminal_write_uint(number);
        terminal_writestring(": ");
        terminal_writestring(command_history[index]);
        terminal_newline();
    }
}

static void print_ideas(void) {
    terminal_writestring("Future ideas for RetroOS:\n");
    terminal_writestring("  * Enhanced history navigation and line editing\n");
    terminal_writestring("  * Basic file system exploration commands\n");
    terminal_writestring("  * Timer interrupts and system clock display\n");
    terminal_writestring("  * Memory management diagnostics\n");
}

static void skip_spaces(const char** str) {
    while (**str == ' ') {
        (*str)++;
    }
}

static int parse_number(const char** str) {
    skip_spaces(str);

    int value = 0;
    int has_digits = 0;

    while (**str >= '0' && **str <= '9') {
        value = value * 10 + (**str - '0');
        (*str)++;
        has_digits = 1;
    }

    if (!has_digits) {
        return -1;
    }

    return value;
}

static void handle_color_command(const char* args) {
    const char* cursor = args;
    int fg = parse_number(&cursor);
    int bg = parse_number(&cursor);

    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
        terminal_writestring("Usage: color <fg> <bg> (values 0-15)\n");
        return;
    }

    terminal_setcolors((uint8_t)fg, (uint8_t)bg);
    terminal_writestring("Text colors updated to fg=");
    terminal_write_uint((unsigned int)fg);
    terminal_writestring(", bg=");
    terminal_write_uint((unsigned int)bg);
    terminal_newline();
}

static void handle_echo_command(const char* args) {
    const char* cursor = args;
    skip_spaces(&cursor);
    if (*cursor == '\0') {
        terminal_writestring("Usage: echo <text>\n");
        return;
    }

    terminal_writestring(cursor);
    terminal_newline();
}

static void history_record(const char* input) {
    if (input[0] == '\0') {
        return;
    }

    size_t destination_index;
    if (history_count < HISTORY_SIZE) {
        destination_index = (history_start + history_count) % HISTORY_SIZE;
        history_count++;
    } else {
        destination_index = history_start;
        history_start = (history_start + 1) % HISTORY_SIZE;
    }

    string_copy(command_history[destination_index], input, COMMAND_BUFFER_SIZE);
    history_total++;
}

static void execute_command(const char* input) {
    if (input[0] == '\0') {
        return;
    }

    if (string_compare(input, "help") == 0) {
        print_help();
        return;
    }

    if (string_compare(input, "about") == 0) {
        print_about();
        return;
    }

    if (string_compare(input, "clear") == 0) {
        terminal_clear();
        print_banner();
        return;
    }

    if (string_compare(input, "history") == 0) {
        print_history();
        return;
    }

    if (string_compare(input, "ideas") == 0) {
        print_ideas();
        return;
    }

    if (string_ncompare(input, "color", 5) == 0 && (input[5] == '\0' || input[5] == ' ')) {
        const char* args = input + 5;
        handle_color_command(args);
        return;
    }

    if (string_ncompare(input, "echo", 4) == 0 && (input[4] == '\0' || input[4] == ' ')) {
        const char* args = input + 4;
        handle_echo_command(args);
        return;
    }

    terminal_writestring("Unknown command. Type 'help' for a list of commands.\n");
}

static void shell_loop(void) {
    char input[COMMAND_BUFFER_SIZE];

    for (;;) {
        terminal_writestring("\nretro> ");
        read_line(input, sizeof(input));
        execute_command(input);
        history_record(input);
    }
}

void kmain(void) {
    terminal_initialize();
    print_banner();
    shell_loop();
}

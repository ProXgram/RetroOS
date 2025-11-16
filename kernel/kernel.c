#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct BootInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t framebuffer;
};

struct InterruptFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};
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

static struct BootInfo g_boot_info;
static volatile uint64_t g_ticks = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "dN"(port));
}
static void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(0x0F, 0x00);
    terminal_buffer = VGA_MEMORY;

static void* kernel_memset(void* dest, int value, size_t count) {
    uint8_t* ptr = (uint8_t*)dest;
    for (size_t i = 0; i < count; i++) {
        ptr[i] = (uint8_t)value;
    }
    return dest;
}

static size_t kernel_strlen(const char* str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
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
    return length;
}

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct IDTEntry g_idt[256];

static void lidt(const struct IDTPointer* pointer) {
    __asm__ volatile ("lidt %0" : : "m"(*pointer));
}

static inline void sti(void) {
    __asm__ volatile ("sti");
}

static inline void cli(void) {
    __asm__ volatile ("cli");
}

static void idt_set_entry(uint8_t vector, uintptr_t handler) {
    struct IDTEntry* entry = &g_idt[vector];
    entry->offset_low = (uint16_t)(handler & 0xFFFF);
    entry->selector = 0x08;
    entry->ist = 0;
    entry->type_attr = 0x8E;
    entry->offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    entry->offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    entry->zero = 0;
}

static void panic(const char* message);

__attribute__((interrupt))
static void default_interrupt_handler(struct InterruptFrame* frame) {
    (void)frame;
}

__attribute__((interrupt))
static void default_interrupt_handler_err(struct InterruptFrame* frame, uint64_t error_code) {
    (void)frame;
    (void)error_code;
    panic("CPU EXCEPTION");
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

static void pic_set_irq_mask(uint8_t master_mask, uint8_t slave_mask) {
    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void pit_init(uint32_t frequency) {
    const uint32_t divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

enum InputEventType {
    INPUT_EVENT_KEY_DOWN,
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_CHAR,
};

struct InputEvent {
    enum InputEventType type;
    uint8_t scancode;
    char character;
};

#define INPUT_QUEUE_CAPACITY 256
static struct InputEvent g_input_queue[INPUT_QUEUE_CAPACITY];
static size_t g_input_head = 0;
static size_t g_input_tail = 0;

static bool input_queue_is_full(void) {
    size_t next_tail = (g_input_tail + 1) % INPUT_QUEUE_CAPACITY;
    return next_tail == g_input_head;
}

static bool input_queue_is_empty(void) {
    return g_input_head == g_input_tail;
}

static void input_queue_push(const struct InputEvent* event) {
    if (input_queue_is_full()) {
        return;
    }
    g_input_queue[g_input_tail] = *event;
    g_input_tail = (g_input_tail + 1) % INPUT_QUEUE_CAPACITY;
}

static bool input_queue_pop(struct InputEvent* out_event) {
    if (input_queue_is_empty()) {
        return false;
    }
    *out_event = g_input_queue[g_input_head];
    g_input_head = (g_input_head + 1) % INPUT_QUEUE_CAPACITY;
    return true;
}

static bool g_shift_active = false;
static bool g_extended_prefix = false;

static const char scancode_map[128] = {
    [0x01] = '\x1B',
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0E] = '\b',
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1C] = '\n',
    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x39] = ' ',
};

static const char scancode_map_shift[128] = {
    [0x01] = '\x1B',
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0E] = '\b',
    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1C] = '\n',
    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x2C] = 'Z',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',
    [0x33] = ',',
    [0x34] = '.',
    [0x39] = ' ',
};

static char keyboard_translate(uint8_t scancode) {
    if (g_shift_active) {
        char shifted = scancode_map_shift[scancode];
        if (shifted != 0) {
            return shifted;
        }
    }
    return scancode_map[scancode];
}

static void keyboard_handle_scancode(uint8_t scancode) {
    if (scancode == 0xE0) {
        g_extended_prefix = true;
        return;
    }

    if (g_extended_prefix) {
        g_extended_prefix = false;
        return;
    }

    const bool released = (scancode & 0x80) != 0;
    const uint8_t code = scancode & 0x7F;

    if (code == 0x2A || code == 0x36) {
        g_shift_active = !released;
        return;
    }

    struct InputEvent key_event;
    key_event.type = released ? INPUT_EVENT_KEY_UP : INPUT_EVENT_KEY_DOWN;
    key_event.scancode = code;
    key_event.character = 0;
    input_queue_push(&key_event);

    if (!released) {
        char translated = keyboard_translate(code);
        if (translated != 0) {
            struct InputEvent char_event;
            char_event.type = INPUT_EVENT_CHAR;
            char_event.scancode = code;
            char_event.character = translated;
            input_queue_push(&char_event);
        }
    }
}

__attribute__((interrupt))
static void irq_timer_handler(struct InterruptFrame* frame) {
    (void)frame;
    g_ticks++;
    pic_send_eoi(0);
}

__attribute__((interrupt))
static void irq_keyboard_handler(struct InterruptFrame* frame) {
    (void)frame;
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0) {
        pic_send_eoi(1);
        return;
    }
    uint8_t scancode = inb(0x60);
    keyboard_handle_scancode(scancode);
    pic_send_eoi(1);
}

static void idt_init(void) {
    kernel_memset(g_idt, 0, sizeof(g_idt));

    for (size_t i = 0; i < 256; i++) {
        idt_set_entry((uint8_t)i, (uintptr_t)default_interrupt_handler);
    }

    const uint8_t fault_vectors_with_error[] = { 8, 10, 11, 12, 13, 14, 17, 30 };
    for (size_t i = 0; i < ARRAY_SIZE(fault_vectors_with_error); i++) {
        idt_set_entry(fault_vectors_with_error[i], (uintptr_t)default_interrupt_handler_err);
    }

    idt_set_entry(32, (uintptr_t)irq_timer_handler);
    idt_set_entry(33, (uintptr_t)irq_keyboard_handler);

    struct IDTPointer pointer;
    pointer.limit = (uint16_t)(sizeof(g_idt) - 1);
    pointer.base = (uint64_t)(uintptr_t)g_idt;
    lidt(&pointer);
}

static void vga_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
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

static void vga_program_palette(void) {
    struct PaletteEntry { uint8_t index; uint8_t r; uint8_t g; uint8_t b; };
    static const struct PaletteEntry palette[] = {
        { 0, 0, 0, 0 },
        { 1, 0, 0, 20 },
        { 2, 0, 20, 0 },
        { 3, 0, 20, 20 },
        { 4, 20, 0, 0 },
        { 5, 20, 0, 20 },
        { 6, 20, 10, 0 },
        { 7, 40, 40, 40 },
        { 8, 20, 20, 20 },
        { 9, 0, 0, 40 },
        { 10, 0, 40, 0 },
        { 11, 0, 40, 40 },
        { 12, 40, 0, 0 },
        { 13, 40, 0, 40 },
        { 14, 40, 40, 0 },
        { 15, 48, 48, 48 },
        { 16, 10, 18, 42 },
        { 17, 6, 10, 24 },
        { 18, 54, 40, 20 },
        { 19, 12, 24, 50 },
        { 20, 8, 16, 32 },
    };

    outb(0x3C6, 0xFF);
    for (size_t i = 0; i < ARRAY_SIZE(palette); i++) {
        const struct PaletteEntry* entry = &palette[i];
        vga_set_palette_entry(entry->index, entry->r, entry->g, entry->b);
    }
}

#define COLOR_BLACK 0
#define COLOR_DEEP_SKY 16
#define COLOR_PANEL 17
#define COLOR_WINDOW 18
#define COLOR_TITLE 19
#define COLOR_TITLE_TEXT 15
#define COLOR_WINDOW_TEXT 15
#define COLOR_STATUS 14
#define COLOR_ACCENT 12

#define MAX_BACKBUFFER_SIZE (320u * 200u)
static uint8_t g_backbuffer[MAX_BACKBUFFER_SIZE];
static uint8_t* g_framebuffer = NULL;
static uint32_t g_framebuffer_width = 0;
static uint32_t g_framebuffer_height = 0;
static uint32_t g_framebuffer_pitch = 0;
static bool g_graphics_ready = false;

static void graphics_present(void) {
    if (!g_graphics_ready) {
        return;
    }

    for (uint32_t y = 0; y < g_framebuffer_height; y++) {
        const uint8_t* src = &g_backbuffer[y * g_framebuffer_width];
        uint8_t* dst = &g_framebuffer[y * g_framebuffer_pitch];
        for (uint32_t x = 0; x < g_framebuffer_width; x++) {
            dst[x] = src[x];
        }
    }
}

static void graphics_clear(uint8_t color) {
    if (!g_graphics_ready) {
        return;
    }
    const size_t total = (size_t)g_framebuffer_width * (size_t)g_framebuffer_height;
    for (size_t i = 0; i < total; i++) {
        g_backbuffer[i] = color;
    }
}

static void graphics_draw_pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (!g_graphics_ready) {
        return;
    }
    if (x >= g_framebuffer_width || y >= g_framebuffer_height) {
        return;
    }
    g_backbuffer[y * g_framebuffer_width + x] = color;
}

static void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color) {
    if (!g_graphics_ready) {
        return;
    }
    for (uint32_t row = 0; row < height; row++) {
        uint32_t py = y + row;
        if (py >= g_framebuffer_height) {
            break;
        }
        uint32_t start = py * g_framebuffer_width;
        for (uint32_t col = 0; col < width; col++) {
            uint32_t px = x + col;
            if (px >= g_framebuffer_width) {
                break;
            }
            g_backbuffer[start + px] = color;
        }
    }
}

struct Glyph {
    char character;
    uint8_t rows[8];
};

static const struct Glyph g_glyphs[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '!', { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 } },
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 } },
    { ':', { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 } },
    { '-', { 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '0', { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 } },
    { '1', { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 } },
    { '2', { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00 } },
    { '3', { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 } },
    { '4', { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 } },
    { '5', { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 } },
    { '6', { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 } },
    { '7', { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 } },
    { '8', { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 } },
    { '9', { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 } },
    { 'A', { 0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00 } },
    { 'B', { 0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00 } },
    { 'C', { 0x3C, 0x62, 0x40, 0x40, 0x40, 0x62, 0x3C, 0x00 } },
    { 'D', { 0x78, 0x44, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00 } },
    { 'E', { 0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00 } },
    { 'F', { 0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00 } },
    { 'G', { 0x3C, 0x62, 0x40, 0x4E, 0x42, 0x62, 0x3E, 0x00 } },
    { 'H', { 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00 } },
    { 'I', { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 } },
    { 'J', { 0x0E, 0x04, 0x04, 0x04, 0x44, 0x64, 0x38, 0x00 } },
    { 'K', { 0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00 } },
    { 'L', { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00 } },
    { 'M', { 0x42, 0x66, 0x5A, 0x5A, 0x42, 0x42, 0x42, 0x00 } },
    { 'N', { 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00 } },
    { 'O', { 0x3C, 0x62, 0x42, 0x42, 0x42, 0x62, 0x3C, 0x00 } },
    { 'P', { 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00 } },
    { 'Q', { 0x3C, 0x62, 0x42, 0x42, 0x4A, 0x64, 0x3A, 0x00 } },
    { 'R', { 0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00 } },
    { 'S', { 0x3C, 0x62, 0x60, 0x3C, 0x06, 0x46, 0x3C, 0x00 } },
    { 'T', { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 } },
    { 'U', { 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3C, 0x00 } },
    { 'V', { 0x42, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x00 } },
    { 'W', { 0x42, 0x42, 0x5A, 0x5A, 0x5A, 0x66, 0x42, 0x00 } },
    { 'X', { 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x42, 0x00 } },
    { 'Y', { 0x42, 0x24, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 } },
    { 'Z', { 0x7E, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7E, 0x00 } },
};

static const uint8_t* glyph_lookup(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 32);
    }
    for (size_t i = 0; i < ARRAY_SIZE(g_glyphs); i++) {
        if (g_glyphs[i].character == character) {
            return g_glyphs[i].rows;
        }
    }
    return g_glyphs[0].rows;
}

static void graphics_draw_char(uint32_t x, uint32_t y, char character, uint8_t color) {
    if (character == '\n') {
        return;
    }
    const uint8_t* rows = glyph_lookup(character);
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                graphics_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void graphics_draw_text(uint32_t x, uint32_t y, const char* text, uint8_t color) {
    uint32_t cursor_x = x;
    uint32_t cursor_y = y;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == '\n') {
            cursor_x = x;
            cursor_y += 10;
            continue;
        }
        graphics_draw_char(cursor_x, cursor_y, c, color);
        cursor_x += 8;
    }
}

static bool graphics_init(const struct BootInfo* info) {
    if (info->bpp != 8) {
        return false;
    }
    if (info->width * info->height > MAX_BACKBUFFER_SIZE) {
        return false;
    }

    g_framebuffer = (uint8_t*)(uintptr_t)info->framebuffer;
    g_framebuffer_width = info->width;
    g_framebuffer_height = info->height;
    g_framebuffer_pitch = info->pitch;

    vga_program_palette();

    g_graphics_ready = true;
    graphics_clear(COLOR_BLACK);
    graphics_present();
    return true;
}

static void format_u64(uint64_t value, char* buffer, size_t capacity) {
    if (capacity == 0) {
        return;
    }
    char temp[32];
    size_t index = 0;
    if (value == 0) {
        temp[index++] = '0';
    } else {
        while (value > 0 && index < ARRAY_SIZE(temp)) {
            temp[index++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    size_t length = (index < capacity - 1) ? index : capacity - 1;
    for (size_t i = 0; i < length; i++) {
        buffer[i] = temp[length - 1 - i];
    }
    buffer[length] = '\0';
}

static void panic(const char* message) {
    cli();
    if (g_graphics_ready) {
        graphics_clear(COLOR_ACCENT);
        graphics_draw_text(16, 16, "KERNEL PANIC", COLOR_TITLE_TEXT);
        graphics_draw_text(16, 32, message, COLOR_TITLE_TEXT);
        graphics_present();
    }
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static char g_text_input[96];
static size_t g_text_length = 0;

static void handle_char_input(char character) {
    if (character == '\b') {
        if (g_text_length > 0) {
            g_text_length--;
            g_text_input[g_text_length] = '\0';
        }
        return;
    }

    if (character == '\n') {
        g_text_length = 0;
        g_text_input[0] = '\0';
        return;
    }

    if (character < 32) {
        return;
    }

    if (g_text_length + 1 >= ARRAY_SIZE(g_text_input)) {
        return;
    }

    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 32);
    }

    g_text_input[g_text_length++] = character;
    g_text_input[g_text_length] = '\0';
}

static void render_desktop(void) {
    graphics_clear(COLOR_DEEP_SKY);

    const uint32_t window_x = 32;
    const uint32_t window_y = 28;
    const uint32_t window_width = g_framebuffer_width - 64;
    const uint32_t window_height = g_framebuffer_height - 56;

    graphics_draw_rect(window_x + 4, window_y + 4, window_width, window_height, COLOR_PANEL);
    graphics_draw_rect(window_x, window_y, window_width, window_height, COLOR_WINDOW);

    graphics_draw_rect(window_x, window_y, window_width, 18, COLOR_TITLE);
    graphics_draw_text(window_x + 8, window_y + 4, "RETROOS GUI FOUNDATION", COLOR_TITLE_TEXT);

    const char* description =
        "GRAPHICAL DESKTOP READY\n"
        "TYPE TO SEE LIVE INPUT."
        ;
    graphics_draw_text(window_x + 12, window_y + 28, description, COLOR_WINDOW_TEXT);

    graphics_draw_text(window_x + 12, window_y + 48, "KEYBOARD INPUT:", COLOR_WINDOW_TEXT);
    graphics_draw_rect(window_x + 12, window_y + 60, window_width - 24, 1, COLOR_ACCENT);
    graphics_draw_text(window_x + 12, window_y + 72, g_text_input, COLOR_WINDOW_TEXT);

    char ticks_buffer[32];
    format_u64(g_ticks, ticks_buffer, sizeof(ticks_buffer));

    char status_line[64];
    const char* prefix = "TICKS:";
    size_t prefix_len = kernel_strlen(prefix);
    for (size_t i = 0; i < prefix_len; i++) {
        status_line[i] = prefix[i];
    }
    size_t j = 0;
    while (ticks_buffer[j] != '\0' && prefix_len + j + 1 < sizeof(status_line)) {
        status_line[prefix_len + j] = ticks_buffer[j];
        j++;
    }
    status_line[prefix_len + j] = '\0';

    char video_line[64];
    const char* video_prefix = "VIDEO:";
    size_t video_prefix_len = kernel_strlen(video_prefix);
    for (size_t i = 0; i < video_prefix_len && i < sizeof(video_line) - 1; i++) {
        video_line[i] = video_prefix[i];
    }
    size_t offset = video_prefix_len;

    char number_buffer[32];
    format_u64(g_boot_info.width, number_buffer, sizeof(number_buffer));
    for (size_t i = 0; number_buffer[i] != '\0' && offset + 1 < sizeof(video_line); i++) {
        video_line[offset++] = number_buffer[i];
    }
    if (offset + 1 < sizeof(video_line)) {
        video_line[offset++] = 'X';
    }
    format_u64(g_boot_info.height, number_buffer, sizeof(number_buffer));
    for (size_t i = 0; number_buffer[i] != '\0' && offset + 1 < sizeof(video_line); i++) {
        video_line[offset++] = number_buffer[i];
    }
    if (offset + 1 < sizeof(video_line)) {
        video_line[offset++] = 'X';
    }
    format_u64(g_boot_info.bpp, number_buffer, sizeof(number_buffer));
    for (size_t i = 0; number_buffer[i] != '\0' && offset + 1 < sizeof(video_line); i++) {
        video_line[offset++] = number_buffer[i];
    }
    video_line[offset] = '\0';

    graphics_draw_text(window_x + 12, window_y + window_height - 36, video_line, COLOR_STATUS);
    graphics_draw_text(window_x + 12, window_y + window_height - 24, status_line, COLOR_STATUS);
}

void kmain(const struct BootInfo* boot_info) {
    g_boot_info = *boot_info;

    cli();
    idt_init();
    pic_remap();
    pit_init(100);
    pic_set_irq_mask(0xFC, 0xFF);

    if (!graphics_init(boot_info)) {
        panic("GRAPHICS INIT FAILED");
    }

    sti();

    for (;;) {
        struct InputEvent event;
        while (input_queue_pop(&event)) {
            if (event.type == INPUT_EVENT_CHAR) {
                handle_char_input(event.character);
            }
        }

        render_desktop();
        graphics_present();
        __asm__ volatile ("hlt");
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
#include "shell.h"
#include "terminal.h"

void kmain(void) {
    terminal_initialize();
    shell_run();
}

void kmain(void) {
    terminal_initialize();
    print_banner();
    shell_loop();
}

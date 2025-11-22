#include "terminal.h"
#include <stdbool.h>
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

/* Scrollback Configuration */
#define HISTORY_LINES 200  // Store up to 200 lines of history

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static size_t terminal_width = VGA_WIDTH;
static size_t terminal_height = VGA_HEIGHT;
static size_t terminal_batch_depth;

/* History Buffer in BSS (RAM) */
static uint16_t g_history[HISTORY_LINES * VGA_WIDTH];
static size_t g_scroll_offset = 0; // 0 = view bottom. >0 = look back N lines.

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void terminal_update_cursor(void) {
    if (terminal_batch_depth > 0) return;

    // If we are scrolled up, hide the cursor effectively
    if (g_scroll_offset > 0) {
        // Move cursor off-screen to 0, height+1
        uint16_t pos = (uint16_t)(VGA_WIDTH * VGA_HEIGHT);
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
        return;
    }

    // Calculate cursor position relative to the visible window (bottom 25 lines)
    // The visual cursor is always at the specific row 0-24 on screen.
    // However, our terminal_row logic might be different if we use a long buffer.
    
    // Simplification: We only draw the cursor if it's in the visible area.
    // Since we force scroll to 0 on typing, the cursor is always visible at the bottom when active.
    
    // We need to map terminal_row (which is 0..HISTORY_LINES-1) to screen row (0..24).
    // Actually, in this implementation, terminal_row will stay bounded 0..24 for logic simplicity,
    // and we shift the buffer contents up.
    
    const uint16_t position = (uint16_t)(terminal_row * VGA_WIDTH + terminal_column);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

// Blit the history buffer to VGA memory based on scroll offset
static void terminal_refresh_screen(void) {
    if (terminal_batch_depth > 0) return;

    // We treat g_history as a ring or linear buffer? 
    // Let's use linear. g_history holds the data.
    // The "active" screen is always the *last* VGA_HEIGHT lines of valid data in history?
    // No, that's complex to manage row indices.
    
    // Strategy: 
    // terminal_row is 0..VGA_HEIGHT-1.
    // When we newline at bottom, we memmove g_history up, discard top line.
    // So g_history[0] is essentially "top of visible screen" if scroll_offset=0?
    // Wait, to support history, we need the buffer to be LARGER than screen.
    
    // NEW STRATEGY: 
    // terminal_row tracks the *logical* cursor line in g_history (0 to HISTORY_LINES-1).
    // When terminal_row hits limit, we shift the whole array up.
    
    size_t start_row_index;
    if (terminal_row < VGA_HEIGHT) {
        start_row_index = 0;
    } else {
        start_row_index = terminal_row - (VGA_HEIGHT - 1);
    }
    
    // Apply scroll offset (looking back)
    if (g_scroll_offset > start_row_index) g_scroll_offset = start_row_index; // Clamp
    start_row_index -= g_scroll_offset;

    volatile uint16_t* vga = VGA_MEMORY;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            size_t hist_idx = (start_row_index + y) * VGA_WIDTH + x;
            vga[y * VGA_WIDTH + x] = g_history[hist_idx];
        }
    }
    
    terminal_update_cursor();
}

void terminal_begin_batch(void) {
    terminal_batch_depth++;
}

void terminal_end_batch(void) {
    if (terminal_batch_depth > 0) terminal_batch_depth--;
    if (terminal_batch_depth == 0) terminal_refresh_screen();
}

static void scroll_buffer_if_needed(void) {
    // If cursor moves past the end of our huge history buffer
    if (terminal_row >= HISTORY_LINES) {
        // Shift entire history up by 1 line
        // 1. Move data
        for (size_t i = 0; i < (HISTORY_LINES - 1) * VGA_WIDTH; i++) {
            g_history[i] = g_history[i + VGA_WIDTH];
        }
        // 2. Clear new last line
        size_t last_row_start = (HISTORY_LINES - 1) * VGA_WIDTH;
        for (size_t i = 0; i < VGA_WIDTH; i++) {
            g_history[last_row_start + i] = vga_entry(' ', terminal_color);
        }
        // 3. Decrement cursor so it stays valid
        terminal_row = HISTORY_LINES - 1;
    }
}

void terminal_initialize(uint32_t width, uint32_t height) {
    terminal_batch_depth = 0;
    terminal_width = (width > 0) ? width : VGA_WIDTH;
    terminal_height = (height > 0) ? height : VGA_HEIGHT;
    terminal_color = make_color(0x0F, 0x01); // White on Blue
    terminal_row = 0;
    terminal_column = 0;
    g_scroll_offset = 0;
    terminal_clear();
}

void terminal_clear(void) {
    terminal_row = 0;
    terminal_column = 0;
    g_scroll_offset = 0;

    // Clear entire history
    for (size_t i = 0; i < HISTORY_LINES * VGA_WIDTH; i++) {
        g_history[i] = vga_entry(' ', terminal_color);
    }
    
    terminal_refresh_screen();
}

void terminal_setcolors(uint8_t fg, uint8_t bg) {
    terminal_color = make_color(fg, bg);
}

void terminal_set_theme(uint8_t fg, uint8_t bg) {
    terminal_color = make_color(fg, bg);
    // Update entire history
    for (size_t i = 0; i < HISTORY_LINES * VGA_WIDTH; i++) {
        uint8_t c = (uint8_t)(g_history[i] & 0xFF);
        g_history[i] = vga_entry((char)c, terminal_color);
    }
    terminal_refresh_screen();
}

void terminal_getcolors(uint8_t* fg, uint8_t* bg) {
    if (fg) *fg = terminal_color & 0x0F;
    if (bg) *bg = (terminal_color >> 4) & 0x0F;
}

static void terminal_setcell(size_t x, size_t y, char c) {
    if (x >= terminal_width || y >= HISTORY_LINES) return;
    g_history[y * VGA_WIDTH + x] = vga_entry(c, terminal_color);
}

void terminal_write_char(char c) {
    // Reset scroll on typing
    if (g_scroll_offset != 0) {
        g_scroll_offset = 0;
        terminal_refresh_screen();
    }

    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        scroll_buffer_if_needed();
        terminal_refresh_screen();
        return;
    }

    if (c == '\r') {
        terminal_column = 0;
        terminal_refresh_screen();
        return;
    }

    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = terminal_width - 1;
        }
        terminal_setcell(terminal_column, terminal_row, ' ');
        terminal_refresh_screen();
        return;
    }

    terminal_setcell(terminal_column, terminal_row, c);
    terminal_column++;
    if (terminal_column >= terminal_width) {
        terminal_column = 0;
        terminal_row++;
        scroll_buffer_if_needed();
    }

    terminal_refresh_screen();
}

void terminal_write(const char* data, size_t length) {
    terminal_begin_batch();
    for (size_t i = 0; i < length; i++) terminal_write_char(data[i]);
    terminal_end_batch();
}

void terminal_writestring(const char* data) {
    terminal_begin_batch();
    for (size_t i = 0; data[i] != '\0'; i++) terminal_write_char(data[i]);
    terminal_end_batch();
}

void terminal_write_uint(unsigned int value) {
    terminal_begin_batch();
    char buffer[12];
    size_t index = 0;
    if (value == 0) {
        terminal_write_char('0');
    } else {
        while (value > 0 && index < sizeof(buffer)) {
            buffer[index++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (index > 0) terminal_write_char(buffer[--index]);
    }
    terminal_end_batch();
}

void terminal_newline(void) {
    terminal_write_char('\n');
}

void terminal_move_cursor_left(size_t count) {
    while (count--) {
        if (terminal_column > 0) terminal_column--;
    }
    terminal_refresh_screen();
}

void terminal_move_cursor_right(size_t count) {
    while (count--) {
        if (terminal_column < terminal_width - 1) terminal_column++;
    }
    terminal_refresh_screen();
}

void terminal_scroll_up(void) {
    // Look further back
    size_t max_scroll;
    if (terminal_row < VGA_HEIGHT) max_scroll = 0;
    else max_scroll = terminal_row - (VGA_HEIGHT - 1);
    
    if (g_scroll_offset < max_scroll) {
        g_scroll_offset++;
        terminal_refresh_screen();
    }
}

void terminal_scroll_down(void) {
    // Look closer to current
    if (g_scroll_offset > 0) {
        g_scroll_offset--;
        terminal_refresh_screen();
    }
}
// testz

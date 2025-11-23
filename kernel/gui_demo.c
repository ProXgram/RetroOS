#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "syslog.h"
#include <stdbool.h>

// Windows XP Style Colors (ARGB)
#define XP_DESKTOP     0xFF5D98D8 // Bliss Blue
#define XP_TASKBAR     0xFF245EDC // Royal Blue
#define XP_START_BTN   0xFF3C8D0D // Vibrant Green
#define XP_WIN_TITLE   0xFF0054E3 // Title Bar Blue
#define XP_WIN_BODY    0xFFECE9D8 // Classic Beige/Grey
#define XP_WHITE       0xFFFFFFFF
#define XP_BLACK       0xFF000000
#define XP_RED         0xFFFF0000

static void draw_start_button(int y) {
    // Green rounded box simulation
    graphics_fill_rect(0, y, 100, 30, XP_START_BTN);
    
    // Windows Flag (rough approximation)
    graphics_fill_rect(10, y + 8,  5, 5, 0xFFE65516); // Red
    graphics_fill_rect(16, y + 8,  5, 5, 0xFF5EC308); // Green
    graphics_fill_rect(10, y + 14, 5, 5, 0xFF2C7EE8); // Blue
    graphics_fill_rect(16, y + 14, 5, 5, 0xFFFFC600); // Yellow

    // Text (Italic-ish)
    graphics_draw_string_scaled(30, y + 8, "start", XP_WHITE, XP_START_BTN, 2);
}

static void draw_window(int x, int y, int w, int h, const char* title) {
    // Shadow
    graphics_fill_rect(x + 4, y + 4, w, h, 0xFF444444);

    // Border
    graphics_fill_rect(x, y, w, h, XP_WIN_TITLE);
    
    // Body (inset from border)
    graphics_fill_rect(x + 3, y + 25, w - 6, h - 28, XP_WIN_BODY);

    // Title Bar Text
    graphics_draw_string_scaled(x + 8, y + 6, title, XP_WHITE, XP_WIN_TITLE, 1);

    // Close Button (Red Square)
    graphics_fill_rect(x + w - 22, y + 4, 18, 18, 0xFFDA3610);
    graphics_draw_char(x + w - 16, y + 9, 'X', XP_WHITE, 0xFFDA3610);
}

static void draw_cursor(int x, int y) {
    // Classic Arrow Cursor
    // We just draw it pixel by pixel or small rects
    uint32_t white = 0xFFFFFFFF;
    uint32_t black = 0xFF000000;
    
    // Outline
    graphics_fill_rect(x, y, 2, 12, black);
    graphics_fill_rect(x, y, 12, 2, black);
    graphics_fill_rect(x+2, y+2, 8, 8, white);
}

void gui_demo_run(void) {
    syslog_write("GUI: Starting XP demo mode");
    
    int width = graphics_get_width();
    int height = graphics_get_height();
    
    // 1. Desktop Background
    graphics_fill_rect(0, 0, width, height, XP_DESKTOP);

    // 2. Taskbar
    int taskbar_h = 30;
    int taskbar_y = height - taskbar_h;
    graphics_fill_rect(0, taskbar_y, width, taskbar_h, XP_TASKBAR);
    // Taskbar Highlight Line
    graphics_fill_rect(0, taskbar_y, width, 2, 0xFF4B84ED);

    // 3. Start Button
    draw_start_button(taskbar_y);

    // 4. Clock Area
    graphics_fill_rect(width - 70, taskbar_y + 2, 70, taskbar_h - 2, 0xFF1C4EBF);
    graphics_draw_string_scaled(width - 60, taskbar_y + 10, "12:00", XP_WHITE, 0xFF1C4EBF, 1);

    // 5. Sample Window
    int win_w = 300;
    int win_h = 160;
    int win_x = (width - win_w) / 2;
    int win_y = (height - win_h) / 2;
    
    draw_window(win_x, win_y, win_w, win_h, "Welcome to Nostalux");

    // Window Content
    graphics_draw_string_scaled(win_x + 20, win_y + 50, "Experience the retro feel.", 0xFF000000, XP_WIN_BODY, 1);
    graphics_draw_string_scaled(win_x + 20, win_y + 80, "Press 'Q' to return to DOS.", 0xFF000000, XP_WIN_BODY, 1);

    // 6. Fake Mouse Cursor
    draw_cursor(win_x + win_w - 10, win_y + win_h + 10);

    // Wait for exit
    while (true) {
        char c = keyboard_poll_char();
        if (c == 'q' || c == 'Q') break;
        timer_wait(5);
    }
    
    syslog_write("GUI: Exiting demo mode");
}

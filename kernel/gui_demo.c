#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "syslog.h"
#include "mouse.h"
#include "kstring.h" // For kstrlen
#include "system.h" // For boot info

#include <stdbool.h>

// --- THEME ---
#define XP_DESKTOP     0xFF5D98D8
#define XP_TASKBAR     0xFF245EDC
#define XP_START_BTN   0xFF3C8D0D
#define XP_WIN_TITLE   0xFF0054E3
#define XP_WIN_BODY    0xFFECE9D8
#define XP_WHITE       0xFFFFFFFF
#define XP_BLACK       0xFF000000
#define XP_RED         0xFFFF0000

// --- STRUCTURES ---
typedef struct {
    int x, y, w, h;
    const char* title;
    bool is_open;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;
} Window;

typedef struct {
    bool menu_open;
    MouseState prev_mouse;
    int width;
    int height;
    // We maintain a "dirty rect" of where the cursor was to erase it
    int cursor_old_x;
    int cursor_old_y;
} Desktop;

// --- STATE ---
static Window g_win_welcome = {
    .x = 200, .y = 150, .w = 300, .h = 160,
    .title = "Welcome to Nostalux",
    .is_open = true,
    .is_dragging = false
};

static Desktop g_desk;

// --- DRAWING HELPERS ---

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    graphics_fill_rect(x, y, w, h, color);
}

static void draw_start_button(int y) {
    draw_rect(0, y, 100, 30, XP_START_BTN);
    // Flag
    draw_rect(10, y + 8,  5, 5, 0xFFE65516);
    draw_rect(16, y + 8,  5, 5, 0xFF5EC308);
    draw_rect(10, y + 14, 5, 5, 0xFF2C7EE8);
    draw_rect(16, y + 14, 5, 5, 0xFFFFC600);
    graphics_draw_string_scaled(30, y + 8, "start", XP_WHITE, XP_START_BTN, 2);
}

static void draw_start_menu(int taskbar_y) {
    if (!g_desk.menu_open) return;
    int w = 200;
    int h = 300;
    int y = taskbar_y - h;
    
    // Header
    draw_rect(0, y, w, 40, 0xFF245EDC);
    graphics_draw_string_scaled(10, y + 10, "User", XP_WHITE, 0xFF245EDC, 2);
    
    // Body (white/blue split)
    draw_rect(0, y + 40, w, h - 40, XP_WHITE);
    draw_rect(w/2, y + 40, w/2, h - 40, 0xFFD3E5FA); // Light blue right side
    
    // Border
    draw_rect(w, y, 2, h, 0xFF0000AA);
    draw_rect(0, y, 2, h, 0xFF0000AA);
    draw_rect(0, y, w, 2, 0xFF0000AA);

    // Items
    graphics_draw_string_scaled(10, y + 60, "Internet", XP_BLACK, XP_WHITE, 1);
    graphics_draw_string_scaled(10, y + 80, "E-mail", XP_BLACK, XP_WHITE, 1);
    
    // Shutdown button
    draw_rect(w/2 + 10, y + h - 40, 70, 25, XP_RED);
    graphics_draw_string_scaled(w/2 + 20, y + h - 32, "Quit", XP_WHITE, XP_RED, 1);
}

static void draw_window_obj(Window* w) {
    if (!w->is_open) return;
    
    // Shadow
    draw_rect(w->x + 4, w->y + 4, w->w, w->h, 0xFF444444);

    // Title Bar
    draw_rect(w->x, w->y, w->w, 25, XP_WIN_TITLE);
    graphics_draw_string_scaled(w->x + 8, w->y + 6, w->title, XP_WHITE, XP_WIN_TITLE, 1);

    // Close Button (Red Square)
    int close_x = w->x + w->w - 22;
    int close_y = w->y + 4;
    draw_rect(close_x, close_y, 18, 18, 0xFFDA3610);
    graphics_draw_char(close_x + 6, close_y + 5, 'X', XP_WHITE, 0xFFDA3610);

    // Body
    draw_rect(w->x, w->y + 25, w->w, w->h - 25, XP_WIN_BODY);
    
    // Borders
    draw_rect(w->x, w->y, 2, w->h, XP_WIN_TITLE); // Left
    draw_rect(w->x + w->w - 2, w->y, 2, w->h, XP_WIN_TITLE); // Right
    draw_rect(w->x, w->y + w->h - 2, w->w, 2, XP_WIN_TITLE); // Bottom

    // Content
    graphics_draw_string_scaled(w->x + 20, w->y + 50, "Experience the retro feel.", 0xFF000000, XP_WIN_BODY, 1);
    graphics_draw_string_scaled(w->x + 20, w->y + 80, "Drag me by the title bar!", 0xFF000000, XP_WIN_BODY, 1);
}

static void draw_cursor_at(int x, int y) {
    uint32_t white = 0xFFFFFFFF;
    uint32_t black = 0xFF000000;
    // Arrow shape
    draw_rect(x, y, 2, 12, black);
    draw_rect(x, y, 12, 2, black);
    draw_rect(x+2, y+2, 8, 8, white);
}

// --- LOGIC ---

static bool point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

static void update_desktop(void) {
    MouseState m = mouse_get_state();
    
    // Detect click (rising edge of left button)
    bool clicked = (m.left_button && !g_desk.prev_mouse.left_button);
    bool released = (!m.left_button && g_desk.prev_mouse.left_button);
    
    int taskbar_h = 30;
    int taskbar_y = g_desk.height - taskbar_h;

    // 1. Handle Start Menu Toggle
    if (clicked) {
        // Start Button
        if (point_in_rect(m.x, m.y, 0, taskbar_y, 100, 30)) {
            g_desk.menu_open = !g_desk.menu_open;
        } 
        // Click outside menu closes it
        else if (g_desk.menu_open) {
             // Check if inside menu
             int menu_w = 200;
             int menu_h = 300;
             int menu_y = taskbar_y - menu_h;
             if (!point_in_rect(m.x, m.y, 0, menu_y, menu_w, menu_h)) {
                 g_desk.menu_open = false;
             } else {
                 // Check "Quit" button inside menu
                 if (point_in_rect(m.x, m.y, menu_w/2 + 10, menu_y + menu_h - 40, 70, 25)) {
                     // Normally this would shutdown, but for demo we just close menu
                     g_desk.menu_open = false;
                 }
             }
        }
    }

    // 2. Window Interaction
    if (g_win_welcome.is_open) {
        // Title Bar Dragging
        if (clicked) {
            // Check close button
            int close_x = g_win_welcome.x + g_win_welcome.w - 22;
            int close_y = g_win_welcome.y + 4;
            if (point_in_rect(m.x, m.y, close_x, close_y, 18, 18)) {
                g_win_welcome.is_open = false;
            }
            // Check title bar
            else if (point_in_rect(m.x, m.y, g_win_welcome.x, g_win_welcome.y, g_win_welcome.w, 25)) {
                g_win_welcome.is_dragging = true;
                g_win_welcome.drag_offset_x = m.x - g_win_welcome.x;
                g_win_welcome.drag_offset_y = m.y - g_win_welcome.y;
            }
        }

        if (g_win_welcome.is_dragging) {
            g_win_welcome.x = m.x - g_win_welcome.drag_offset_x;
            g_win_welcome.y = m.y - g_win_welcome.drag_offset_y;
        }

        if (released) {
            g_win_welcome.is_dragging = false;
        }
    }

    g_desk.prev_mouse = m;
}

static void render_full_frame(void) {
    // NOTE: In a real OS we would use double buffering or dirty rectangles.
    // Here we redraw everything every frame. This causes flickering but is simple.
    
    // Background
    draw_rect(0, 0, g_desk.width, g_desk.height, XP_DESKTOP);

    // Taskbar
    int taskbar_h = 30;
    int taskbar_y = g_desk.height - taskbar_h;
    draw_rect(0, taskbar_y, g_desk.width, taskbar_h, XP_TASKBAR);
    draw_rect(0, taskbar_y, g_desk.width, 2, 0xFF4B84ED); // Highlight
    
    // Start Button
    draw_start_button(taskbar_y);
    
    // Clock
    draw_rect(g_desk.width - 70, taskbar_y + 2, 70, taskbar_h - 2, 0xFF1C4EBF);
    // (Static time for demo, can be hooked to RTC)
    graphics_draw_string_scaled(g_desk.width - 60, taskbar_y + 10, "12:00", XP_WHITE, 0xFF1C4EBF, 1);

    // Windows
    draw_window_obj(&g_win_welcome);

    // Start Menu (on top of windows)
    draw_start_menu(taskbar_y);

    // Cursor (on top of everything)
    MouseState m = mouse_get_state();
    draw_cursor_at(m.x, m.y);
}

void gui_demo_run(void) {
    syslog_write("GUI: Initializing subsystems...");
    
    mouse_init(); // Initialize PS/2 Mouse Driver
    
    g_desk.width = graphics_get_width();
    g_desk.height = graphics_get_height();
    g_desk.menu_open = false;
    g_desk.prev_mouse = mouse_get_state();

    // Reset window position
    g_win_welcome.is_open = true;
    g_win_welcome.x = (g_desk.width - g_win_welcome.w) / 2;
    g_win_welcome.y = (g_desk.height - g_win_welcome.h) / 2;

    while (true) {
        // Exit on 'Q'
        char c = keyboard_poll_char();
        if (c == 'q' || c == 'Q') break;
        
        // Re-open window on 'R'
        if (c == 'r' || c == 'R') g_win_welcome.is_open = true;

        update_desktop();
        render_full_frame();
        
        // Software VSync / limiter
        // Wait for ~16ms (60 FPS target)
        // Since we don't have hardware VSync, we just sleep.
        // Flickering is expected without double buffering.
        uint64_t start = timer_get_ticks();
        while (timer_get_ticks() < start + 2) {
             __asm__ volatile("hlt");
        }
    }
    
    syslog_write("GUI: Exiting...");
}

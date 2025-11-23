#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "io.h"
#include "kstdio.h" // For sprintf-style formatting if available, else manual
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 4
#define WIN_CAPTION_H 26
#define TASKBAR_H 32

// --- Theme Colors (Windows 7 Basic / Aero-ish) ---
#define COL_DESKTOP     0xFF2D73A8
#define COL_TASKBAR     0xFF18334E
#define COL_START_BTN   0xFF1F4E79
#define COL_START_HOVER 0xFF3465A4
#define COL_WIN_ACTIVE  0xFF6B95BD
#define COL_WIN_INACT   0xFF888888
#define COL_WIN_BODY    0xFFF0F0F0
#define COL_BTN_HOVER   0xFF4F81BD
#define COL_WHITE       0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_RED         0xFFE81123
#define COL_TEXT_SHADOW 0xFF000000

// --- Application Types ---
typedef enum {
    APP_NONE,
    APP_WELCOME,
    APP_NOTEPAD,
    APP_CALC
} AppType;

// --- Structures ---
typedef struct {
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool dragging;
    int drag_off_x;
    int drag_off_y;
    
    // App Specific State
    char text_buffer[128]; // For Notepad
    int text_len;
} Window;

typedef struct {
    bool open;
    int x, y, w, h;
} StartMenu;

// --- Global State ---
static Window windows[MAX_WINDOWS];
static StartMenu start_menu;
static MouseState mouse;
static MouseState prev_mouse;
static int screen_w, screen_h;
static int focused_win_idx = -1;

// --- Helpers ---

static bool point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

static void bring_to_front(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    // Simple swap to end? No, shift array.
    // We want windows[idx] to move to windows[MAX_WINDOWS-1] (top)
    // But we need to preserve order of others.
    // For simplicity in this demo: We just mark it focused. 
    // True Z-order requires a separate list of pointers or indices.
    // We will just update the `focused_win_idx` and draw the focused one LAST.
    focused_win_idx = idx;
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int i = 0;
    int n = v;
    char tmp[16];
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

// --- Initialization ---

static void init_window(int idx, AppType type, const char* title, int x, int y, int w, int h) {
    windows[idx].id = idx;
    windows[idx].type = type;
    // Manual strcpy
    for(int i=0; i<32; i++) {
        windows[idx].title[i] = title[i];
        if (title[i] == 0) break;
    }
    windows[idx].x = x;
    windows[idx].y = y;
    windows[idx].w = w;
    windows[idx].h = h;
    windows[idx].visible = true;
    windows[idx].minimized = false;
    windows[idx].dragging = false;
    windows[idx].text_len = 0;
    windows[idx].text_buffer[0] = 0;
}

static void gui_init(void) {
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    
    // Taskbar
    start_menu.w = 180;
    start_menu.h = 240;
    start_menu.x = 0;
    start_menu.y = screen_h - TASKBAR_H - start_menu.h;
    start_menu.open = false;

    // Clear windows
    for(int i=0; i<MAX_WINDOWS; i++) windows[i].visible = false;

    // Spawn Defaults
    init_window(0, APP_WELCOME, "Welcome", 50, 50, 300, 180);
    init_window(1, APP_NOTEPAD, "Notepad", 400, 80, 250, 200);
    init_window(2, APP_CALC,    "Calc",    100, 300, 160, 220);
    
    focused_win_idx = 0;
}

// --- Drawing ---

static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    graphics_fill_rect(x, y, w, h, col);
}

static void draw_button(int x, int y, int w, int h, const char* label, bool hover) {
    uint32_t bg = hover ? COL_BTN_HOVER : 0xFFDDDDDD;
    uint32_t border = 0xFF888888;
    
    draw_rect(x, y, w, h, bg);
    draw_rect(x, y, w, 1, COL_WHITE);
    draw_rect(x, y, 1, h, COL_WHITE);
    draw_rect(x + w - 1, y, 1, h, border);
    draw_rect(x, y + h - 1, w, 1, border);
    
    int len = kstrlen(label) * 8;
    graphics_draw_string_scaled(x + (w - len)/2, y + (h - 8)/2, label, COL_BLACK, bg, 1);
}

static void draw_window_content(Window* w) {
    int cx = w->x + 4;
    int cy = w->y + WIN_CAPTION_H;
    int cw = w->w - 8;
    int ch = w->h - WIN_CAPTION_H - 4;
    
    draw_rect(cx, cy, cw, ch, COL_WIN_BODY);

    if (w->type == APP_WELCOME) {
        graphics_draw_string_scaled(cx + 10, cy + 20, "Welcome to Nostalux!", COL_BLACK, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(cx + 10, cy + 40, "Fully interactive GUI.", 0xFF555555, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(cx + 10, cy + 70, "Drag windows, click", 0xFF555555, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(cx + 10, cy + 85, "Start, or type in Notepad.", 0xFF555555, COL_WIN_BODY, 1);
    }
    else if (w->type == APP_NOTEPAD) {
        // White paper background
        draw_rect(cx + 2, cy + 2, cw - 4, ch - 4, COL_WHITE);
        // Draw text buffer
        graphics_draw_string_scaled(cx + 6, cy + 6, w->text_buffer, COL_BLACK, COL_WHITE, 1);
        // Cursor
        int cursor_x = cx + 6 + (w->text_len * 8);
        if ((timer_get_ticks() / 50) % 2 == 0) { // Blink
            draw_rect(cursor_x, cy + 6, 2, 10, COL_BLACK);
        }
    }
    else if (w->type == APP_CALC) {
        // Display
        draw_rect(cx + 10, cy + 10, cw - 20, 25, COL_WHITE);
        draw_rect(cx + 10, cy + 10, cw - 20, 1, 0xFF888888); // inset shadow
        graphics_draw_string_scaled(cx + 15, cy + 18, "0", COL_BLACK, COL_WHITE, 1);
        
        // Buttons Grid
        const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
        int bx = cx + 10;
        int by = cy + 45;
        for (int i = 0; i < 16; i++) {
            int row = i / 4;
            int col = i % 4;
            bool hover = point_in_rect(mouse.x, mouse.y, bx + col*35, by + row*30, 30, 25);
            draw_button(bx + col*35, by + row*30, 30, 25, btns[i], hover);
        }
    }
}

static void draw_window_frame(Window* w, bool active) {
    if (!w->visible || w->minimized) return;

    uint32_t border = active ? COL_WIN_ACTIVE : COL_WIN_INACT;
    uint32_t title_col = active ? COL_BLACK : 0xFF555555;

    // Shadow
    draw_rect(w->x + 6, w->y + 6, w->w, w->h, 0x40000000);

    // Border
    draw_rect(w->x, w->y, w->w, w->h, border);
    draw_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, border);

    // Title Bar
    draw_rect(w->x + 3, w->y + 3, w->w - 6, WIN_CAPTION_H - 3, border); // Gradient placeholder
    graphics_draw_string_scaled(w->x + 8, w->y + 8, w->title, title_col, border, 1);

    // Controls
    int btn_sz = 18;
    int close_x = w->x + w->w - 24;
    int min_x   = close_x - 22;
    int btn_y   = w->y + 5;

    bool hover_close = point_in_rect(mouse.x, mouse.y, close_x, btn_y, btn_sz, btn_sz);
    draw_rect(close_x, btn_y, btn_sz, btn_sz, hover_close ? COL_RED : 0xFFCC8888);
    graphics_draw_char(close_x + 5, btn_y + 5, 'X', COL_WHITE, hover_close ? COL_RED : 0xFFCC8888);

    bool hover_min = point_in_rect(mouse.x, mouse.y, min_x, btn_y, btn_sz, btn_sz);
    draw_rect(min_x, btn_y, btn_sz, btn_sz, hover_min ? COL_BTN_HOVER : 0xFF88AACC);
    graphics_draw_char(min_x + 5, btn_y + 5, '_', COL_WHITE, hover_min ? COL_BTN_HOVER : 0xFF88AACC);

    // Content
    draw_window_content(w);
}

static void draw_taskbar_ui(void) {
    int y = screen_h - TASKBAR_H;
    draw_rect(0, y, screen_w, TASKBAR_H, COL_TASKBAR);
    draw_rect(0, y, screen_w, 1, 0xFF607080); // Highlight

    // Start Button
    bool hover_start = point_in_rect(mouse.x, mouse.y, 0, y, 80, TASKBAR_H);
    uint32_t start_col = hover_start ? COL_START_HOVER : COL_START_BTN;
    draw_rect(0, y, 80, TASKBAR_H, start_col);
    graphics_draw_string_scaled(15, y + 10, "Start", COL_WHITE, start_col, 1);

    // Task Buttons
    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible) {
            bool active = (i == focused_win_idx) && !windows[i].minimized;
            bool hover = point_in_rect(mouse.x, mouse.y, bx, y + 2, 100, TASKBAR_H - 4);
            uint32_t bg = active ? 0xFF3A6EA5 : (hover ? 0xFF4A7EB5 : 0xFF2A4E75);
            
            draw_rect(bx, y + 2, 100, TASKBAR_H - 4, bg);
            graphics_draw_string_scaled(bx + 10, y + 12, windows[i].title, COL_WHITE, bg, 1);
            
            bx += 105;
        }
    }

    // Clock
    draw_rect(screen_w - 60, y + 2, 60, TASKBAR_H - 4, 0xFF102030);
    graphics_draw_string_scaled(screen_w - 50, y + 12, "12:00", COL_WHITE, 0xFF102030, 1);
}

static void draw_start_menu_ui(void) {
    if (!start_menu.open) return;
    int x = start_menu.x;
    int y = start_menu.y;
    int w = start_menu.w;
    int h = start_menu.h;

    draw_rect(x, y, w, h, COL_WHITE);
    draw_rect(x, y, w, 1, 0xFF888888); // Border
    draw_rect(x + w - 1, y, 1, h, 0xFF888888);
    draw_rect(x, y, 1, h, 0xFF888888);

    // User Header
    draw_rect(x + 2, y + 2, w - 4, 40, COL_WIN_ACTIVE);
    graphics_draw_string_scaled(x + 10, y + 14, "Guest", COL_WHITE, COL_WIN_ACTIVE, 1);

    // Items
    const char* items[] = {"Notepad", "Calculator", "My Computer", "Run...", "Shutdown"};
    int iy = y + 50;
    for (int i = 0; i < 5; i++) {
        bool hover = point_in_rect(mouse.x, mouse.y, x + 2, iy, w - 4, 25);
        uint32_t bg = hover ? COL_BTN_HOVER : COL_WHITE;
        uint32_t fg = hover ? COL_WHITE : COL_BLACK;
        
        draw_rect(x + 2, iy, w - 4, 25, bg);
        graphics_draw_string_scaled(x + 10, iy + 8, items[i], fg, bg, 1);
        iy += 30;
    }
}

static void draw_cursor_sprite(void) {
    int x = mouse.x;
    int y = mouse.y;
    uint32_t border = COL_BLACK;
    uint32_t fill = COL_WHITE;
    
    // Arrow shape
    draw_rect(x, y, 2, 14, border);
    draw_rect(x, y, 12, 2, border);
    draw_rect(x+2, y+2, 8, 10, fill);
    draw_rect(x+2, y+12, 2, 2, border); // Tip
}

// --- Input Handling ---

static void handle_input(void) {
    // Keyboard for Notepad
    if (focused_win_idx != -1 && windows[focused_win_idx].type == APP_NOTEPAD) {
        char c = keyboard_poll_char();
        if (c != 0) {
            Window* w = &windows[focused_win_idx];
            if (c == '\b') {
                if (w->text_len > 0) w->text_buffer[--w->text_len] = 0;
            } else if (c >= 32 && c <= 126 && w->text_len < 30) { // Limit length for demo
                w->text_buffer[w->text_len++] = c;
                w->text_buffer[w->text_len] = 0;
            }
        }
    } else {
        // Discard key if no focus (or handle shortcuts)
        keyboard_poll_char(); 
    }

    // Mouse Interaction
    bool click = mouse.left_button && !prev_mouse.left_button;
    bool release = !mouse.left_button && prev_mouse.left_button;

    int tb_y = screen_h - TASKBAR_H;

    // 1. Check Start Menu
    if (start_menu.open) {
        if (click) {
            // Check items
            int iy = start_menu.y + 50;
            if (point_in_rect(mouse.x, mouse.y, start_menu.x, iy, start_menu.w, 25)) { /* Notepad */ 
                windows[1].visible = true; windows[1].minimized = false; bring_to_front(1); start_menu.open = false; 
            }
            else if (point_in_rect(mouse.x, mouse.y, start_menu.x, iy+30, start_menu.w, 25)) { /* Calc */ 
                windows[2].visible = true; windows[2].minimized = false; bring_to_front(2); start_menu.open = false; 
            }
            else if (point_in_rect(mouse.x, mouse.y, start_menu.x, iy+120, start_menu.w, 25)) { /* Shutdown */ 
                // Just exit GUI for now
                start_menu.open = false; // Or trigger exit flag
            }
            else if (!point_in_rect(mouse.x, mouse.y, start_menu.x, start_menu.y, start_menu.w, start_menu.h)) {
                start_menu.open = false; // Clicked outside
            }
        }
    }

    // 2. Check Taskbar
    if (click && mouse.y >= tb_y) {
        // Start Button
        if (point_in_rect(mouse.x, mouse.y, 0, tb_y, 80, TASKBAR_H)) {
            start_menu.open = !start_menu.open;
        }
        // Task Buttons
        else {
            int bx = 90;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].visible) {
                    if (point_in_rect(mouse.x, mouse.y, bx, tb_y, 100, TASKBAR_H)) {
                        if (i == focused_win_idx && !windows[i].minimized) {
                            windows[i].minimized = true;
                        } else {
                            windows[i].minimized = false;
                            bring_to_front(i);
                        }
                    }
                    bx += 105;
                }
            }
        }
    }

    // 3. Check Windows (Front to Back logic for clicks, but we iterate all)
    // We need to check focused first to avoid clicking "through" windows
    if (click && !start_menu.open && mouse.y < tb_y) {
        bool hit = false;
        
        // Check focused first
        if (focused_win_idx != -1 && windows[focused_win_idx].visible && !windows[focused_win_idx].minimized) {
            Window* w = &windows[focused_win_idx];
            if (point_in_rect(mouse.x, mouse.y, w->x, w->y, w->w, w->h)) {
                hit = true;
                // Check Drag
                if (mouse.y < w->y + WIN_CAPTION_H) {
                    // Check Close
                    if (mouse.x > w->x + w->w - 24) w->visible = false;
                    // Check Min
                    else if (mouse.x > w->x + w->w - 48) w->minimized = true;
                    else {
                        w->dragging = true;
                        w->drag_off_x = mouse.x - w->x;
                        w->drag_off_y = mouse.y - w->y;
                    }
                }
                // Check Content (Calc Buttons)
                else if (w->type == APP_CALC) {
                    // Simple logic: if clicked anywhere in buttons area, just append '1' for demo
                    // Real logic requires mapping the grid again.
                    if (point_in_rect(mouse.x, mouse.y, w->x + 10, w->y + 45, 140, 120)) {
                        // Demo: Add '1'
                        // w->text_buffer... etc
                    }
                }
            }
        }

        // If not hit focused, find top-most other window
        if (!hit) {
            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                if (windows[i].visible && !windows[i].minimized) {
                    if (point_in_rect(mouse.x, mouse.y, windows[i].x, windows[i].y, windows[i].w, windows[i].h)) {
                        bring_to_front(i);
                        // We don't process drag/close on the click that focuses it to prevent accidents
                        break; 
                    }
                }
            }
        }
    }

    // Dragging
    if (focused_win_idx != -1 && windows[focused_win_idx].dragging) {
        if (!mouse.left_button) {
            windows[focused_win_idx].dragging = false;
        } else {
            windows[focused_win_idx].x = mouse.x - windows[focused_win_idx].drag_off_x;
            windows[focused_win_idx].y = mouse.y - windows[focused_win_idx].drag_off_y;
        }
    }
}

// --- Main Entry ---

void gui_demo_run(void) {
    syslog_write("GUI: Starting Desktop Environment");
    mouse_init(); // Re-init to be sure
    gui_init();

    bool running = true;
    while (running) {
        prev_mouse = mouse;
        mouse = mouse_get_state(); // Update from driver

        // Exit Combo (Top Left Corner + Right Click)
        if (mouse.x < 5 && mouse.y < 5 && mouse.right_button) running = false;

        handle_input();

        // Render (Painter's Algorithm)
        // 1. Desktop
        draw_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
        
        // 2. Icons
        graphics_draw_string_scaled(20, 20, "My PC", COL_WHITE, COL_DESKTOP, 1);
        graphics_draw_string_scaled(20, 60, "Trash", COL_WHITE, COL_DESKTOP, 1);

        // 3. Windows (Background ones)
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (i != focused_win_idx) draw_window_frame(&windows[i], false);
        }
        // 4. Focused Window
        if (focused_win_idx != -1) draw_window_frame(&windows[focused_win_idx], true);

        // 5. Taskbar & Menus
        draw_taskbar_ui();
        draw_start_menu_ui();

        // 6. Cursor
        draw_cursor_sprite();

        // VSync / Delay
        timer_wait(1);
    }
    
    // Clear screen on exit
    graphics_fill_rect(0, 0, screen_w, screen_h, 0xFF000000);
}

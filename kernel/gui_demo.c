#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "io.h"
#include "kstdio.h"
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 10
#define WIN_CAPTION_H 24
#define TASKBAR_H 32
#define WIN_MIN_W 100
#define WIN_MIN_H 50

// --- Theme: Aero / Win7 Basic ---
#define COL_DESKTOP     0xFF2D73A8
#define COL_TASKBAR     0xFF18334E
#define COL_START_BTN   0xFF1F4E79
#define COL_START_HOVER 0xFF3465A4
#define COL_WIN_ACTIVE  0xFF6B95BD
#define COL_WIN_INACT   0xFF888888
#define COL_WIN_BODY    0xFFF0F0F0
#define COL_BTN_HOVER   0xFF4F81BD
#define COL_BTN_PRESS   0xFF2F518D
#define COL_WHITE       0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_RED         0xFFE81123
#define COL_TEXT_SHADOW 0xFF000000

// --- Application Logic ---

typedef enum {
    APP_NONE = 0,
    APP_WELCOME,
    APP_NOTEPAD,
    APP_CALC,
    APP_SYSINFO
} AppType;

// Calc State
typedef struct {
    int current_val;
    int accumulator;
    char op; // '+', '-', '*', '/'
    bool new_entry;
} CalcState;

// Notepad State
typedef struct {
    char buffer[256];
    int length;
    int cursor_pos;
} NotepadState;

// Window Structure
typedef struct WindowStruct {
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool focused;
    
    // Dragging
    bool dragging;
    int drag_off_x;
    int drag_off_y;

    // App Data (Union for memory efficiency)
    union {
        CalcState calc;
        NotepadState notepad;
    } state;
} Window;

// Global Desktop State
static Window windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;

// --- Helpers ---

static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void str_copy(char* dest, const char* src) {
    int i = 0;
    while (src[i] && i < 31) { dest[i] = src[i]; i++; }
    dest[i] = 0;
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    bool neg = v < 0;
    if (neg) v = -v;
    
    char tmp[16];
    int i = 0;
    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    if (neg) tmp[i++] = '-';
    
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

// --- Window Manager Core ---

static Window* get_focused_window(void) {
    // In our array, the last visible non-minimized window is top-most
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (windows[i].visible && !windows[i].minimized) return &windows[i];
    }
    return NULL;
}

static void focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return;
    if (!windows[index].visible) return;

    // Move to end of array (Top of Z-order)
    Window temp = windows[index];
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[MAX_WINDOWS - 1] = temp;
    
    // Update flags
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;
    windows[MAX_WINDOWS - 1].focused = true;
    windows[MAX_WINDOWS - 1].minimized = false;
}

static Window* create_window(AppType type, const char* title, int w, int h) {
    // Find empty slot or recycle lowest Z-order hidden window
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) {
            slot = i;
            break;
        }
    }
    
    // If full, overwrite bottom-most (index 0)
    if (slot == -1) slot = 0;

    Window* win = &windows[slot];
    win->id = slot; // ID isn't unique across time but works for index
    win->type = type;
    str_copy(win->title, title);
    win->w = w;
    win->h = h;
    win->x = (screen_w - w) / 2 + (slot * 20); // Cascade
    win->y = (screen_h - h) / 2 + (slot * 20);
    win->visible = true;
    win->minimized = false;
    win->dragging = false;
    
    // Init App State
    if (type == APP_NOTEPAD) {
        win->state.notepad.length = 0;
        win->state.notepad.buffer[0] = 0;
    } else if (type == APP_CALC) {
        win->state.calc.current_val = 0;
        win->state.calc.accumulator = 0;
        win->state.calc.op = 0;
        win->state.calc.new_entry = true;
    }

    focus_window(slot);
    return &windows[MAX_WINDOWS - 1]; // Since focus_window moved it to top
}

// --- GUI Rendering Primitives ---

static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    graphics_fill_rect(x, y, w, h, col);
}

static void draw_bevel_rect(int x, int y, int w, int h, uint32_t fill, bool sunken) {
    draw_rect(x, y, w, h, fill);
    uint32_t top = sunken ? 0xFF555555 : 0xFFFFFFFF;
    uint32_t bot = sunken ? 0xFFFFFFFF : 0xFF555555;
    
    draw_rect(x, y, w, 1, top);
    draw_rect(x, y, 1, h, top);
    draw_rect(x, y+h-1, w, 1, bot);
    draw_rect(x+w-1, y, 1, h, bot);
}

static void draw_button(int x, int y, int w, int h, const char* label, bool pressed) {
    uint32_t col = pressed ? COL_BTN_PRESS : 0xFFDDDDDD;
    draw_bevel_rect(x, y, w, h, col, pressed);
    int tw = kstrlen(label) * 8;
    int tx = x + (w - tw)/2;
    int ty = y + (h - 8)/2;
    if (pressed) { tx++; ty++; }
    uint32_t tcol = pressed ? COL_WHITE : COL_BLACK;
    graphics_draw_string_scaled(tx, ty, label, tcol, col, 1);
}

// --- Application Rendering ---

static void render_notepad(Window* w, int client_x, int client_y) {
    // Text Area
    draw_bevel_rect(client_x + 2, client_y + 2, w->w - 4, w->h - WIN_CAPTION_H - 4, COL_WHITE, true);
    
    // Content
    graphics_draw_string_scaled(client_x + 6, client_y + 6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
    
    // Blinking Cursor
    if ((timer_get_ticks() / 25) % 2) {
        int cursor_x = client_x + 6 + (w->state.notepad.length * 8);
        draw_rect(cursor_x, client_y + 6, 2, 10, COL_BLACK);
    }
}

static void render_calc(Window* w, int client_x, int client_y) {
    // Display
    char disp[16];
    int_to_str(w->state.calc.current_val, disp);
    
    draw_bevel_rect(client_x + 10, client_y + 10, w->w - 20, 25, COL_WHITE, true);
    graphics_draw_string_scaled(client_x + w->w - 20 - (kstrlen(disp)*8), client_y + 18, disp, COL_BLACK, COL_WHITE, 1);

    // Buttons Grid
    const char* btns[] = { "7", "8", "9", "/", 
                           "4", "5", "6", "*", 
                           "1", "2", "3", "-", 
                           "C", "0", "=", "+" };
    
    int bx = client_x + 10;
    int by = client_y + 45;
    int bw = 35, bh = 25;
    
    for (int i = 0; i < 16; i++) {
        int r = i / 4;
        int c = i % 4;
        int b_x = bx + c * (bw + 5);
        int b_y = by + r * (bh + 5);
        
        bool hover = rect_contains(b_x, b_y, bw, bh, mouse.x, mouse.y);
        bool press = hover && mouse.left_button;
        
        draw_button(b_x, b_y, bw, bh, btns[i], press);
    }
}

static void render_window(Window* w) {
    if (!w->visible || w->minimized) return;

    uint32_t frame_col = w->focused ? COL_WIN_ACTIVE : COL_WIN_INACT;
    uint32_t title_fg  = COL_BLACK;

    // Shadow
    draw_rect(w->x + 6, w->y + 6, w->w, w->h, 0x40000000);

    // Frame
    draw_rect(w->x, w->y, w->w, w->h, frame_col);
    draw_rect(w->x + 2, w->y + 2, w->w - 4, w->h - 4, frame_col);

    // Title Bar
    draw_rect(w->x + 3, w->y + 3, w->w - 6, WIN_CAPTION_H - 3, frame_col); 
    graphics_draw_string_scaled(w->x + 8, w->y + 8, w->title, title_fg, frame_col, 1);

    // Controls
    int btn_sz = 18;
    int close_x = w->x + w->w - 24;
    int min_x   = close_x - 22;
    int btn_y   = w->y + 5;

    bool hover_close = rect_contains(close_x, btn_y, btn_sz, btn_sz, mouse.x, mouse.y);
    draw_rect(close_x, btn_y, btn_sz, btn_sz, hover_close ? COL_RED : 0xFFCC8888);
    graphics_draw_char(close_x + 5, btn_y + 5, 'X', COL_WHITE, hover_close ? COL_RED : 0xFFCC8888);

    bool hover_min = rect_contains(min_x, btn_y, btn_sz, btn_sz, mouse.x, mouse.y);
    draw_rect(min_x, btn_y, btn_sz, btn_sz, hover_min ? COL_BTN_HOVER : 0xFF99BBDD);
    graphics_draw_char(min_x + 5, btn_y + 5, '_', COL_WHITE, hover_min ? COL_BTN_HOVER : 0xFF99BBDD);

    // Client Area
    int client_x = w->x + 4;
    int client_y = w->y + WIN_CAPTION_H + 4;
    int client_w = w->w - 8;
    int client_h = w->h - WIN_CAPTION_H - 8;
    draw_rect(client_x, client_y, client_w, client_h, COL_WIN_BODY);

    // Render App
    if (w->type == APP_NOTEPAD) render_notepad(w, client_x, client_y);
    else if (w->type == APP_CALC) render_calc(w, client_x, client_y);
    else {
        graphics_draw_string_scaled(client_x + 10, client_y + 10, "Welcome to Nostalux!", COL_BLACK, COL_WIN_BODY, 1);
    }
}

static void render_desktop(void) {
    // Wallpaper
    draw_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
    
    // Icons
    graphics_draw_string_scaled(20, 20, "My PC", COL_WHITE, COL_DESKTOP, 1);
    graphics_draw_string_scaled(20, 60, "Notepad", COL_WHITE, COL_DESKTOP, 1);
    graphics_draw_string_scaled(20, 100, "Calc", COL_WHITE, COL_DESKTOP, 1);

    // Windows (Back to Front)
    for (int i = 0; i < MAX_WINDOWS; i++) {
        render_window(&windows[i]);
    }

    // Taskbar
    int ty = screen_h - TASKBAR_H;
    draw_rect(0, ty, screen_w, TASKBAR_H, COL_TASKBAR);
    draw_rect(0, ty, screen_w, 2, 0xFF607080); // Highlight

    // Start Button
    bool hover_s = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    uint32_t scol = (hover_s || start_menu_open) ? COL_START_HOVER : COL_START_BTN;
    draw_rect(0, ty, 80, TASKBAR_H, scol);
    graphics_draw_string_scaled(15, ty + 10, "Start", COL_WHITE, scol, 1);

    // Taskbar Tabs
    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible) {
            bool active = windows[i].focused && !windows[i].minimized;
            uint32_t bcol = active ? 0xFF3A6EA5 : 0xFF2A4E75;
            draw_bevel_rect(bx, ty + 2, 100, TASKBAR_H - 4, bcol, active);
            
            char buf[12];
            int k = 0; while(k<10 && windows[i].title[k]) { buf[k] = windows[i].title[k]; k++; }
            buf[k] = 0;
            graphics_draw_string_scaled(bx+5, ty+10, buf, COL_WHITE, bcol, 1);
            
            bx += 105;
        }
    }

    // Start Menu
    if (start_menu_open) {
        int mw = 160, mh = 200;
        int my = ty - mh;
        draw_bevel_rect(0, my, mw, mh, COL_WIN_BODY, false);
        
        // Items
        int iy = my + 10;
        graphics_draw_string_scaled(10, iy, "Calculator", COL_BLACK, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(10, iy+30, "Notepad", COL_BLACK, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(10, iy+60, "About", COL_BLACK, COL_WIN_BODY, 1);
        
        // Shutdown
        draw_rect(0, my + mh - 40, mw, 40, 0xFF333333);
        graphics_draw_string_scaled(10, my + mh - 28, "Shutdown", COL_WHITE, 0xFF333333, 1);
    }

    // Cursor
    int cx = mouse.x, cy = mouse.y;
    draw_rect(cx, cy, 2, 14, COL_BLACK);
    draw_rect(cx, cy, 12, 2, COL_BLACK);
    draw_rect(cx+2, cy+2, 8, 8, COL_WHITE);
}

// --- Input Handling ---

static void handle_calc_input(Window* w, char op) {
    CalcState* s = &w->state.calc;
    
    if (op >= '0' && op <= '9') {
        int d = op - '0';
        if (s->new_entry) {
            s->current_val = d;
            s->new_entry = false;
        } else {
            // Avoid overflow demo
            if (s->current_val < 10000000)
                s->current_val = s->current_val * 10 + d;
        }
    } else if (op == 'C') {
        s->current_val = 0;
        s->accumulator = 0;
        s->op = 0;
        s->new_entry = true;
    } else if (op == '+' || op == '-' || op == '*' || op == '/') {
        s->accumulator = s->current_val;
        s->op = op;
        s->new_entry = true;
    } else if (op == '=') {
        if (s->op == '+') s->current_val = s->accumulator + s->current_val;
        else if (s->op == '-') s->current_val = s->accumulator - s->current_val;
        else if (s->op == '*') s->current_val = s->accumulator * s->current_val;
        else if (s->op == '/') {
            if (s->current_val != 0) s->current_val = s->accumulator / s->current_val;
            else s->current_val = 0; // Err
        }
        s->op = 0;
        s->new_entry = true;
    }
}

static void handle_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;

    // 1. Start Menu Toggle
    if (rect_contains(0, ty, 80, TASKBAR_H, x, y)) {
        start_menu_open = !start_menu_open;
        return;
    }

    // 2. Start Menu Items
    if (start_menu_open && rect_contains(0, ty - 200, 160, 200, x, y)) {
        int local_y = y - (ty - 200);
        if (local_y > 160) { // Shutdown
            outw(0x604, 0x2000); // QEMU Shutdown
        } else if (local_y < 30) {
            create_window(APP_CALC, "Calculator", 200, 250);
        } else if (local_y < 60) {
            create_window(APP_NOTEPAD, "Notepad", 300, 200);
        } else if (local_y < 90) {
            create_window(APP_WELCOME, "About", 250, 150);
        }
        start_menu_open = false;
        return;
    }
    
    // Close menu if clicked outside
    if (start_menu_open) {
        start_menu_open = false;
        return;
    }

    // 3. Taskbar Windows
    if (y >= ty) {
        int bx = 90;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].visible) {
                if (rect_contains(bx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i].focused && !windows[i].minimized) {
                        windows[i].minimized = true;
                    } else {
                        windows[i].minimized = false;
                        focus_window(i);
                    }
                    return;
                }
                bx += 105;
            }
        }
        return;
    }

    // 4. Windows (Hit Test Top-Down)
    // Iterate backwards through array, as last element is top-most
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = &windows[i];
        if (!w->visible || w->minimized) continue;

        if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
            focus_window(i);
            // Get pointer to moved window (it's now at MAX_WINDOWS-1)
            w = &windows[MAX_WINDOWS - 1]; 

            // Controls
            int cx = w->x + w->w - 24;
            int mx = cx - 22;
            int cy = w->y + 4;

            if (rect_contains(cx, cy, 18, 18, x, y)) { // Close
                w->visible = false;
                return;
            }
            if (rect_contains(mx, cy, 18, 18, x, y)) { // Min
                w->minimized = true;
                return;
            }
            if (y < w->y + WIN_CAPTION_H) { // Drag
                w->dragging = true;
                w->drag_off_x = x - w->x;
                w->drag_off_y = y - w->y;
                return;
            }

            // App Interaction
            if (w->type == APP_CALC) {
                // Check buttons
                const char* btns = "789/456*123-C0=+";
                int bx = w->x + 14, by = w->y + WIN_CAPTION_H + 49;
                int bw = 35, bh = 25;
                for(int b=0; b<16; b++) {
                    int r = b/4, c = b%4;
                    if (rect_contains(bx + c*(bw+5), by + r*(bh+5), bw, bh, x, y)) {
                        handle_calc_input(w, btns[b]);
                    }
                }
            }
            return;
        }
    }

    // 5. Desktop Icons
    if (rect_contains(20, 20, 60, 30, x, y)) create_window(APP_SYSINFO, "My PC", 250, 150);
    if (rect_contains(20, 60, 60, 30, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    if (rect_contains(20, 100, 60, 30, x, y)) create_window(APP_CALC, "Calculator", 200, 250);
}

static void handle_keyboard(char c) {
    Window* w = get_focused_window();
    if (!w) return;

    if (w->type == APP_NOTEPAD) {
        NotepadState* ns = &w->state.notepad;
        if (c == '\b') {
            if (ns->length > 0) ns->buffer[--ns->length] = 0;
        } else if (c >= 32 && c <= 126) {
            if (ns->length < 255) {
                ns->buffer[ns->length++] = c;
                ns->buffer[ns->length] = 0;
            }
        }
    }
}

// --- Main Entry ---

void gui_demo_run(void) {
    syslog_write("GUI: Starting Desktop Environment...");
    mouse_init();
    
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    
    // Reset Windows
    for(int i=0; i<MAX_WINDOWS; i++) windows[i].visible = false;
    
    // Spawn Welcome
    create_window(APP_WELCOME, "Welcome", 300, 160);

    bool running = true;
    while (running) {
        // Keyboard
        char c = keyboard_poll_char();
        if (c == 27) running = false; // Esc
        if (c) handle_keyboard(c);

        // Mouse
        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        bool click = (mouse.left_button && !prev_mouse.left_button);
        bool release = (!mouse.left_button && prev_mouse.left_button);

        if (click) handle_click(mouse.x, mouse.y);
        if (release) {
            for(int i=0; i<MAX_WINDOWS; i++) windows[i].dragging = false;
        }
        
        // Dragging
        if (mouse.left_button) {
            Window* top = &windows[MAX_WINDOWS-1];
            if (top->visible && top->dragging) {
                top->x = mouse.x - top->drag_off_x;
                top->y = mouse.y - top->drag_off_y;
            }
        }

        // Render
        render_desktop();
        
        // Loop
        timer_wait(1);
    }
    
    // Clear screen on exit
    graphics_fill_rect(0, 0, screen_w, screen_h, 0xFF000000);
}

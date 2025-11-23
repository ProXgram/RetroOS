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
#define MAX_WINDOWS 16
#define WIN_CAPTION_H 26
#define TASKBAR_H 32

// --- Colors ---
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
#define COL_RED_HOVER   0xFFFF4444
#define COL_GRAY        0xFFCCCCCC

// --- Types ---
typedef enum { APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC } AppType;

typedef struct {
    int current_val;
    int accumulator;
    char op;
    bool new_entry;
} CalcState;

typedef struct {
    char buffer[256];
    int length;
} NotepadState;

typedef struct {
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool focused;
    bool dragging;
    int drag_off_x, drag_off_y;
    union { CalcState calc; NotepadState notepad; } state;
} Window;

// --- State ---
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
    int i = 0; char tmp[16];
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

// Moves window at 'index' to the end of the array (top of Z-order)
static void focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return;
    
    // If already at top, just set focus flag
    if (index == MAX_WINDOWS - 1) {
        for(int i=0; i<MAX_WINDOWS-1; i++) windows[i].focused = false;
        windows[MAX_WINDOWS-1].focused = true;
        windows[MAX_WINDOWS-1].minimized = false;
        return;
    }

    Window temp = windows[index];
    // Shift everyone else down to fill the gap
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    // Place target at top
    windows[MAX_WINDOWS - 1] = temp;
    
    // Update focus flags
    for(int i=0; i<MAX_WINDOWS; i++) windows[i].focused = false;
    windows[MAX_WINDOWS - 1].focused = true;
    windows[MAX_WINDOWS - 1].minimized = false;
}

static Window* create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    // 1. Try to find an unused slot
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) { slot = i; break; }
    }
    // 2. If full, steal the bottom-most window (index 0)
    if (slot == -1) slot = 0; 

    Window* win = &windows[slot];
    win->id = slot; // Debug ID
    win->type = type;
    str_copy(win->title, title);
    win->w = w; win->h = h;
    
    // Cascade positioning
    static int cascade_offset = 0;
    win->x = 60 + cascade_offset;
    win->y = 60 + cascade_offset;
    cascade_offset = (cascade_offset + 30) % 200;
    
    // Boundary check
    if (win->x + w > screen_w) win->x = 20;
    if (win->y + h > screen_h - TASKBAR_H) win->y = 20;

    win->visible = true;
    win->minimized = false;
    win->dragging = false;
    
    if (type == APP_NOTEPAD) {
        win->state.notepad.length = 0;
        win->state.notepad.buffer[0] = 0;
    } else if (type == APP_CALC) {
        win->state.calc.current_val = 0;
        win->state.calc.new_entry = true;
    }

    focus_window(slot);
    return &windows[MAX_WINDOWS - 1];
}

// --- Drawing ---

static void draw_bevel_rect(int x, int y, int w, int h, uint32_t fill, bool sunk) {
    graphics_fill_rect(x, y, w, h, fill);
    uint32_t tl = sunk ? 0xFF555555 : 0xFFFFFFFF; // Top-Left color
    uint32_t br = sunk ? 0xFFFFFFFF : 0xFF555555; // Bottom-Right color
    
    graphics_fill_rect(x, y, w, 1, tl);      // Top
    graphics_fill_rect(x, y, 1, h, tl);      // Left
    graphics_fill_rect(x, y+h-1, w, 1, br);  // Bottom
    graphics_fill_rect(x+w-1, y, 1, h, br);  // Right
}

static void render_notepad(Window* w, int cx, int cy) {
    draw_bevel_rect(cx+2, cy+2, w->w-4, w->h-WIN_CAPTION_H-4, COL_WHITE, true);
    graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
    
    // Cursor
    if ((timer_get_ticks() / 15) % 2) {
        int cur_x = cx + 6 + (w->state.notepad.length * 8);
        graphics_fill_rect(cur_x, cy+6, 2, 10, COL_BLACK);
    }
}

static void render_calc(Window* w, int cx, int cy) {
    // Screen
    char buf[16];
    int_to_str(w->state.calc.current_val, buf);
    draw_bevel_rect(cx+10, cy+10, w->w-20, 30, COL_WHITE, true);
    
    // Right align number
    int text_w = kstrlen(buf) * 8 * 2; // Scale 2
    graphics_draw_string_scaled(cx+w->w-20-text_w, cy+15, buf, COL_BLACK, COL_WHITE, 2);

    const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    int start_x = cx+10;
    int start_y = cy+50;
    
    for(int i=0; i<16; i++) {
        int r = i/4;
        int c = i%4;
        int b_x = start_x + c*40;
        int b_y = start_y + r*35;
        
        bool hover = rect_contains(b_x, b_y, 35, 30, mouse.x, mouse.y);
        bool press = hover && mouse.left_button;
        
        uint32_t btn_col = press ? COL_BTN_PRESS : (hover ? COL_BTN_HOVER : 0xFFDDDDDD);
        draw_bevel_rect(b_x, b_y, 35, 30, btn_col, press);
        
        int tx = b_x + 12; 
        int ty = b_y + 10;
        if(press) { tx++; ty++; }
        
        graphics_draw_char(tx, ty, btns[i][0], COL_BLACK, btn_col);
    }
}

static void render_window(Window* w) {
    if (!w->visible || w->minimized) return;

    uint32_t frame_col = w->focused ? COL_WIN_ACTIVE : COL_WIN_INACT;
    
    // 1. Window Border/Background
    // Drop shadow
    graphics_fill_rect(w->x + 4, w->y + 4, w->w, w->h, 0x50000000); 
    // Main body
    graphics_fill_rect(w->x, w->y, w->w, w->h, frame_col);
    graphics_fill_rect(w->x+2, w->y+2, w->w-4, w->h-4, frame_col);

    // 2. Title Bar
    graphics_fill_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H, frame_col);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_BLACK, frame_col, 1);

    // 3. Close Button (Top Right)
    // Hitbox: (x + w - 24, y + 4) size 20x20
    int close_x = w->x + w->w - 24;
    int close_y = w->y + 4;
    bool hover_close = rect_contains(close_x, close_y, 20, 20, mouse.x, mouse.y);
    uint32_t close_col = hover_close ? COL_RED_HOVER : COL_RED;
    
    draw_bevel_rect(close_x, close_y, 20, 20, close_col, false);
    graphics_draw_char(close_x + 6, close_y + 6, 'X', COL_WHITE, close_col);

    // 4. Client Area
    int cx = w->x + 4;
    int cy = w->y + WIN_CAPTION_H + 4;
    int cw = w->w - 8;
    int ch = w->h - WIN_CAPTION_H - 8;
    graphics_fill_rect(cx, cy, cw, ch, COL_WIN_BODY);

    if (w->type == APP_NOTEPAD) render_notepad(w, cx, cy);
    else if (w->type == APP_CALC) render_calc(w, cx, cy);
    else {
        graphics_draw_string_scaled(cx+20, cy+40, "Welcome!", COL_BLACK, COL_WIN_BODY, 2);
    }
}

static void render_desktop(void) {
    // Desktop BG
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
    
    // Desktop Icons
    struct { int x, y; const char* lbl; } icons[] = {
        {20, 20, "My PC"},
        {20, 80, "Notepad"},
        {20, 140, "Calc"},
    };
    
    for (int i=0; i<3; i++) {
        bool hover = rect_contains(icons[i].x, icons[i].y, 60, 50, mouse.x, mouse.y);
        uint32_t bg = hover ? 0x40FFFFFF : COL_DESKTOP;
        if (hover) graphics_fill_rect(icons[i].x, icons[i].y, 60, 50, bg);
        
        // Draw icon "graphic" (a generic white box)
        graphics_fill_rect(icons[i].x+15, icons[i].y+5, 30, 25, COL_WHITE);
        graphics_draw_string_scaled(icons[i].x+5, icons[i].y+35, icons[i].lbl, COL_WHITE, bg, 1);
    }

    // Windows
    for (int i=0; i<MAX_WINDOWS; i++) render_window(&windows[i]);

    // Taskbar
    int ty = screen_h - TASKBAR_H;
    graphics_fill_rect(0, ty, screen_w, TASKBAR_H, COL_TASKBAR);
    graphics_fill_rect(0, ty, screen_w, 2, 0xFF607080);
    
    // Start Button
    bool hover_start = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    uint32_t start_col = (hover_start || start_menu_open) ? COL_START_HOVER : COL_START_BTN;
    draw_bevel_rect(2, ty+2, 78, TASKBAR_H-4, start_col, start_menu_open);
    graphics_draw_string_scaled(20, ty+10, "Start", COL_WHITE, start_col, 1);
    
    // Taskbar Tabs
    int tx = 90;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i].visible) {
            bool active = windows[i].focused && !windows[i].minimized;
            uint32_t tcol = active ? 0xFF3A6EA5 : 0xFF2A4E75;
            draw_bevel_rect(tx, ty+4, 100, TASKBAR_H-8, tcol, active);
            
            char short_title[11];
            int c=0; while(c<10 && windows[i].title[c]){short_title[c]=windows[i].title[c]; c++;} short_title[c]=0;
            graphics_draw_string_scaled(tx+5, ty+10, short_title, COL_WHITE, tcol, 1);
            tx += 105;
        }
    }

    // Start Menu
    if (start_menu_open) {
        int mw = 150;
        int mh = 200;
        int my = ty - mh;
        draw_bevel_rect(0, my, mw, mh, COL_WIN_BODY, false);
        
        // Items
        struct { int y; const char* lbl; } items[] = {
            {my+10, "Calculator"},
            {my+40, "Notepad"},
            {my+160, "Shutdown"}
        };
        
        for(int i=0; i<3; i++) {
            bool h = rect_contains(2, items[i].y, mw-4, 25, mouse.x, mouse.y);
            uint32_t bg = h ? COL_BTN_HOVER : COL_WIN_BODY;
            uint32_t fg = h ? COL_WHITE : COL_BLACK;
            if(h) graphics_fill_rect(2, items[i].y, mw-4, 25, bg);
            graphics_draw_string_scaled(10, items[i].y+8, items[i].lbl, fg, bg, 1);
        }
    }

    // Mouse Cursor
    int mx = mouse.x;
    int my = mouse.y;
    // Simple Arrow
    graphics_fill_rect(mx, my, 2, 16, COL_BLACK);
    graphics_fill_rect(mx, my, 12, 2, COL_BLACK);
    graphics_fill_rect(mx+2, my+2, 8, 10, COL_WHITE);
    graphics_put_pixel(mx, my, COL_WHITE); // Tip
}

// --- Input Logic ---

static void handle_calc_logic(Window* w, char c) {
    CalcState* s = &w->state.calc;
    if (c >= '0' && c <= '9') {
        int d = c - '0';
        if (s->new_entry) { s->current_val = d; s->new_entry = false; }
        else if (s->current_val < 100000000) s->current_val = s->current_val * 10 + d;
    } else if (c == 'C') {
        s->current_val = 0; s->accumulator = 0; s->op = 0; s->new_entry = true;
    } else if (c=='+'||c=='-'||c=='*'||c=='/') {
        s->accumulator = s->current_val; s->op = c; s->new_entry = true;
    } else if (c == '=') {
        if (s->op == '+') s->current_val = s->accumulator + s->current_val;
        else if (s->op == '-') s->current_val = s->accumulator - s->current_val;
        else if (s->op == '*') s->current_val = s->accumulator * s->current_val;
        else if (s->op == '/' && s->current_val != 0) s->current_val = s->accumulator / s->current_val;
        s->op = 0; s->new_entry = true;
    }
}

// Global click handler
static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;

    // 1. Start Menu Interaction
    if (start_menu_open) {
        int mw = 150;
        int mh = 200;
        int my = ty - mh;
        if (rect_contains(0, my, mw, mh, x, y)) {
            // Clicked inside menu
            if (rect_contains(0, my+10, mw, 25, x, y)) create_window(APP_CALC, "Calculator", 220, 300);
            else if (rect_contains(0, my+40, mw, 25, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(0, my+160, mw, 25, x, y)) outw(0x604, 0x2000); // Shutdown
            
            start_menu_open = false;
            return;
        }
        // Clicked outside menu -> close it
        start_menu_open = false;
        // Don't return, allow click to pass through to desktop/windows behind
    }

    // 2. Taskbar
    if (y >= ty) {
        // Start Button
        if (x < 80) {
            start_menu_open = !start_menu_open;
            return;
        }
        
        // Window Tabs
        int tx = 90;
        for (int i=0; i<MAX_WINDOWS; i++) {
            if (windows[i].visible) {
                if (rect_contains(tx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i].focused && !windows[i].minimized) {
                        windows[i].minimized = true;
                    } else {
                        windows[i].minimized = false;
                        focus_window(i);
                    }
                    return;
                }
                tx += 105;
            }
        }
        return;
    }

    // 3. Windows (Top-down iteration for Z-order)
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = &windows[i];
        if (!w->visible || w->minimized) continue;
        
        // Hit detection
        if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
            // Bring to front (modifies array!)
            focus_window(i);
            
            // Re-acquire pointer to the top window (since we just moved it there)
            w = &windows[MAX_WINDOWS - 1]; 
            
            // Check specific controls
            
            // Close Button
            if (rect_contains(w->x + w->w - 24, w->y + 4, 20, 20, x, y)) {
                w->visible = false;
                return;
            }
            
            // Title Bar (Drag start)
            if (y < w->y + WIN_CAPTION_H) {
                w->dragging = true;
                w->drag_off_x = x - w->x;
                w->drag_off_y = y - w->y;
                return;
            }
            
            // Calculator Buttons
            if (w->type == APP_CALC) {
                const char* btns = "789/456*123-C0=+";
                int start_x = w->x + 14; 
                int start_y = w->y + WIN_CAPTION_H + 54;
                for(int b=0; b<16; b++) {
                    int r = b/4; int c = b%4;
                    if (rect_contains(start_x + c*40, start_y + r*35, 35, 30, x, y)) {
                        handle_calc_logic(w, btns[b]);
                        return;
                    }
                }
            }
            
            return; // Click consumed by window
        }
    }

    // 4. Desktop Icons
    if (rect_contains(20, 20, 60, 50, x, y)) create_window(APP_WELCOME, "My PC", 300, 200);
    else if (rect_contains(20, 80, 60, 50, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 140, 60, 50, x, y)) create_window(APP_CALC, "Calc", 220, 300);
}

// --- Main Loop ---

void gui_demo_run(void) {
    syslog_write("GUI: Starting demo...");
    
    graphics_enable_double_buffer();
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    mouse_init();

    // Reset State
    for(int i=0; i<MAX_WINDOWS; i++) windows[i].visible = false;
    start_menu_open = false;
    
    // Initial Window
    create_window(APP_WELCOME, "Welcome", 300, 160);

    bool running = true;
    while(running) {
        // 1. Keyboard
        char c = keyboard_poll_char();
        if (c == 27) running = false; // ESC
        
        Window* top = &windows[MAX_WINDOWS-1];
        if (c && top->visible && !top->minimized && top->type == APP_NOTEPAD) {
            NotepadState* ns = &top->state.notepad;
            if (c == '\b') {
                if (ns->length > 0) ns->buffer[--ns->length] = 0;
            } else if (c >= 32 && c <= 126) {
                if (ns->length < 250) {
                    ns->buffer[ns->length++] = c;
                    ns->buffer[ns->length] = 0;
                }
            }
        }

        // 2. Mouse
        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        // Dragging
        if (mouse.left_button && top->visible && top->dragging) {
            top->x = mouse.x - top->drag_off_x;
            top->y = mouse.y - top->drag_off_y;
        }

        // Click Event (Edge detection)
        if (mouse.left_button && !prev_mouse.left_button) {
            on_click(mouse.x, mouse.y);
        }
        
        // Release Event
        if (!mouse.left_button && prev_mouse.left_button) {
            for(int i=0; i<MAX_WINDOWS; i++) windows[i].dragging = false;
        }

        // 3. Render
        render_desktop();
        graphics_swap_buffer();
        
        // No wait logic (runs as fast as possible for responsiveness)
    }
    
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
}
